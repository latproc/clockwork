#include <Plugin.h>
#include <debug_malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <read_file.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <curl/curl.h>
#include "cJSON.h"
#include "exec_web_request.h"
#include "stdio.h"
#include "assert.h"
#include <ctype.h>
#include <pthread.h>
#include <stdatomic.h>
#include <strings.h>

/*
 * WEBREQUEST plugin
 *
 * Each Start enqueues work on a fixed worker pool (default 4 threads).
 * Workers stay alive so libcurl/TLS allocations reuse the same arenas
 * instead of creating a short-lived pthread per request (the production
 * RSS growth pattern on glibc).
 *
 * done/abort are atomic; the control path never races plain ints across
 * threads. Completion still joins no per-request thread — the pool owns
 * the threads for process life.
 */

#ifndef WEBREQUEST_POOL_SIZE_DEFAULT
#define WEBREQUEST_POOL_SIZE_DEFAULT 4
#endif

struct WebRequestData {
    /* Set by control thread before enqueue; read by worker. */
    char *request;
    char *post_data;
    char *result;
    char *errors;
    const int64_t *status;
    int trust_host_certificate;
    char *content_type;
    char *method;
    /*
     * Owned request snapshot. getStringValue() allocates this on the control
     * thread before enqueue; the worker only reads it. done_cleanup frees it
     * after the worker publishes done. Never retain the live CW property.
     */
    char *headers_json;
    /* Worker-produced JSON snapshot of the final response headers. */
    char *response_headers_json;

    /* Cross-thread flags (control thread + worker). */
    atomic_int done;
    atomic_int abort;

    /* Control-thread only. */
    int running;
    long http_code;
};

/* ---- fixed worker pool ------------------------------------------------ */

struct PoolJob {
    struct WebRequestData *data;
    struct PoolJob *next;
};

static pthread_once_t pool_once = PTHREAD_ONCE_INIT;
static pthread_mutex_t pool_mu = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t pool_cv = PTHREAD_COND_INITIALIZER;
static struct PoolJob *pool_head = NULL;
static struct PoolJob *pool_tail = NULL;
static int pool_started = 0;
static int pool_size = WEBREQUEST_POOL_SIZE_DEFAULT;
static pthread_t *pool_threads = NULL;

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static int curl_initialized = 0;
static void *initialisation_scope = 0;

static void curl_global_init_once(void) {
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        log_message_2(initialisation_scope, "curl_global_init failed: ", curl_easy_strerror(rc));
    } else {
        curl_initialized = 1;
    }
}

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct WebRequestData *data = (struct WebRequestData *)userp;
    if (atomic_load_explicit(&data->abort, memory_order_relaxed)) {
        return 0;
    }
    size_t old_len = data->result ? strlen(data->result) : 0;
    char *new_buf = 0;
    if (data->result) {
        new_buf = (char *)realloc(data->result, old_len + realsize + 1);
    } else {
        new_buf = (char *)malloc(realsize + 1);
    }
    if (!new_buf) {
        return 0;
    }
    memcpy(new_buf + old_len, contents, realsize);
    new_buf[old_len + realsize] = '\0';
    data->result = new_buf;
    return realsize;
}

struct HeaderCapture {
    cJSON *object;
};

static size_t header_cb(char *buffer, size_t size, size_t nmemb, void *userp) {
    const size_t length = size * nmemb;
    struct HeaderCapture *capture = (struct HeaderCapture *)userp;
    if (!capture || !capture->object || !buffer || length == 0) return length;

    /* A redirect/proxy response starts another header block; retain only final headers. */
    if (length >= 5 && strncasecmp(buffer, "HTTP/", 5) == 0) {
        cJSON_Delete(capture->object);
        capture->object = cJSON_CreateObject();
        return capture->object ? length : 0;
    }

    const char *colon = memchr(buffer, ':', length);
    if (!colon) return length;
    size_t name_length = (size_t)(colon - buffer);
    const char *value = colon + 1;
    const char *end = buffer + length;
    while (value < end && (*value == ' ' || *value == '\t')) ++value;
    while (end > value && (end[-1] == '\r' || end[-1] == '\n' ||
                           end[-1] == ' ' || end[-1] == '\t')) --end;
    if (name_length == 0) return length;

    char *name_copy = (char *)malloc(name_length + 1);
    char *value_copy = (char *)malloc((size_t)(end - value) + 1);
    if (!name_copy || !value_copy) {
        free(name_copy);
        free(value_copy);
        return 0;
    }
    memcpy(name_copy, buffer, name_length);
    name_copy[name_length] = '\0';
    for (size_t i = 0; i < name_length; ++i) {
        name_copy[i] = (char)tolower((unsigned char)name_copy[i]);
    }
    memcpy(value_copy, value, (size_t)(end - value));
    value_copy[end - value] = '\0';

    cJSON *item = cJSON_CreateString(value_copy);
    int ok = item != NULL;
    if (item) {
        if (cJSON_GetObjectItem(capture->object, name_copy)) {
            cJSON_ReplaceItemInObject(capture->object, name_copy, item);
        } else {
            cJSON_AddItemToObject(capture->object, name_copy, item);
        }
    }
    free(name_copy);
    free(value_copy);
    return ok ? length : 0;
}

static int xferinfo_cb(void *clientp,
                       curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    struct WebRequestData *data = (struct WebRequestData *)clientp;
    if (data && atomic_load_explicit(&data->abort, memory_order_relaxed)) {
        return 1;
    }
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return 0;
}

static int progress_cb(void *clientp,
                       double dltotal, double dlnow,
                       double ultotal, double ulnow) {
    struct WebRequestData *data = (struct WebRequestData *)clientp;
    if (data && atomic_load_explicit(&data->abort, memory_order_relaxed)) {
        return 1;
    }
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return 0;
}

/* Per-worker easy handle: reuse reduces TLS/session setup and allocator noise. */
static __thread CURL *tls_curl = NULL;

static int contains_crlf(const char *value) {
    return value && (strchr(value, '\r') || strchr(value, '\n'));
}

static int valid_header_name(const char *name) {
    const unsigned char *p = (const unsigned char *)name;
    if (!p || !*p) {
        return 0;
    }
    for (; *p; ++p) {
        if (!(isalnum(*p) || *p == '-')) {
            return 0;
        }
    }
    return 1;
}

static int append_header(struct WebRequestData *data,
                         struct curl_slist **headers,
                         const char *name,
                         const char *value) {
    if (!valid_header_name(name) || !value || contains_crlf(value)) {
        if (!data->errors) {
            data->errors = debug_strdup("Invalid HTTP header name or value", "errors");
        }
        return 0;
    }

    size_t length = strlen(name) + 2 + strlen(value) + 1;
    char *line = (char *)malloc(length);
    if (!line) {
        if (!data->errors) {
            data->errors = debug_strdup("malloc failed building HTTP header", "errors");
        }
        return 0;
    }
    snprintf(line, length, "%s: %s", name, value);

    /* curl_slist_append does not consume line and leaves the old list valid on failure. */
    struct curl_slist *updated = curl_slist_append(*headers, line);
    free(line);
    if (!updated) {
        if (!data->errors) {
            data->errors = debug_strdup("Failed to append HTTP header", "errors");
        }
        return 0;
    }
    *headers = updated;
    return 1;
}

static int append_json_headers(struct WebRequestData *data,
                               struct curl_slist **headers,
                               int *has_content_type) {
    if (!data->headers_json || !*data->headers_json ||
        strcmp(data->headers_json, "null") == 0) {
        return 1;
    }

    cJSON *root = cJSON_Parse(data->headers_json);
    if (!root) {
        data->errors = debug_strdup("Headers must contain valid JSON", "errors");
        return 0;
    }
    if (root->type != cJSON_Object) {
        cJSON_Delete(root);
        data->errors = debug_strdup("Headers must be a JSON object", "errors");
        return 0;
    }

    for (cJSON *item = root->child; item; item = item->next) {
        if (!item->string || item->type != cJSON_String || !item->valuestring) {
            cJSON_Delete(root);
            data->errors =
                debug_strdup("HTTP header names and values must be strings", "errors");
            return 0;
        }
        if (strcasecmp(item->string, "Content-Type") == 0) {
            *has_content_type = 1;
        }
        if (!append_header(data, headers, item->string, item->valuestring)) {
            cJSON_Delete(root);
            return 0;
        }
    }

    cJSON_Delete(root);
    return 1;
}

/* Run one HTTP request. Allocates response on this worker thread. */
static void perform_request(struct WebRequestData *data) {
    pthread_once(&curl_once, curl_global_init_once);

    if (!tls_curl) {
        tls_curl = curl_easy_init();
    } else {
        curl_easy_reset(tls_curl);
    }
    CURL *curl = tls_curl;
    if (!curl) {
        if (!data->errors) {
            data->errors = debug_strdup("curl_easy_init failed", "errors");
        }
        atomic_store_explicit(&data->done, 1, memory_order_release);
        return;
    }

    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, data);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, data);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    struct curl_slist *headers = NULL;
    struct HeaderCapture response_headers = {cJSON_CreateObject()};
    int has_content_type = 0;

    if (!response_headers.object) {
        data->errors = debug_strdup("Unable to allocate response headers", "errors");
        atomic_store_explicit(&data->done, 1, memory_order_release);
        return;
    }

    curl_easy_setopt(curl, CURLOPT_URL, data->request);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, header_cb);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response_headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ClockworkWebRequest/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (data->trust_host_certificate) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    int headers_ok = append_json_headers(data, &headers, &has_content_type);
    if (headers_ok && !has_content_type && data->content_type && *data->content_type) {
        headers_ok = append_header(data, &headers, "Content-Type", data->content_type);
    }

    if (headers_ok) {
        if (headers) {
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        }

        const char *method = (data->method && *data->method) ? data->method : NULL;
        if (method) {
            if (strcasecmp(method, "GET") == 0) {
                curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            } else if (strcasecmp(method, "POST") == 0) {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
            } else if (strcasecmp(method, "HEAD") == 0) {
                curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "HEAD");
            } else {
                curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
            }
        }

        if (data->post_data && *data->post_data) {
            if (!method) {
                curl_easy_setopt(curl, CURLOPT_POST, 1L);
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data->post_data);
            } else if (strcasecmp(method, "HEAD") == 0) {
                /* no body */
            } else {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data->post_data);
            }
        }

        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK && !data->errors) {
            data->errors = debug_strdup(curl_easy_strerror(res), "errors");
        }
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &data->http_code);
    }

    if (headers) {
        curl_slist_free_all(headers);
    }
    if (response_headers.object) {
        data->response_headers_json = cJSON_PrintUnformatted(response_headers.object);
        cJSON_Delete(response_headers.object);
        if (!data->response_headers_json && !data->errors) {
            data->errors = debug_strdup("Unable to serialize response headers", "errors");
        }
    }
    /* Keep tls_curl for the next job on this worker; only reset next time. */

    atomic_store_explicit(&data->done, 1, memory_order_release);
}

static void *pool_worker(void *arg) {
    (void)arg;
    for (;;) {
        struct PoolJob *job = NULL;

        pthread_mutex_lock(&pool_mu);
        while (pool_head == NULL) {
            pthread_cond_wait(&pool_cv, &pool_mu);
        }
        job = pool_head;
        pool_head = job->next;
        if (pool_head == NULL) {
            pool_tail = NULL;
        }
        pthread_mutex_unlock(&pool_mu);

        if (job->data) {
            perform_request(job->data);
        }
        free(job);
    }
    return NULL;
}

static void pool_init_once(void) {
    const char *env = getenv("WEBREQUEST_POOL_SIZE");
    if (env && *env) {
        int n = atoi(env);
        if (n >= 1 && n <= 32) {
            pool_size = n;
        }
    }

    pool_threads = (pthread_t *)calloc((size_t)pool_size, sizeof(pthread_t));
    if (!pool_threads) {
        pool_size = 0;
        return;
    }

    for (int i = 0; i < pool_size; ++i) {
        int rc = pthread_create(&pool_threads[i], NULL, pool_worker, NULL);
        if (rc != 0) {
            /* Keep whatever threads started; reduce size so we do not join dead slots. */
            pool_size = i;
            break;
        }
    }
    pool_started = (pool_size > 0);
}

static int enqueue_request(struct WebRequestData *data) {
    pthread_once(&pool_once, pool_init_once);
    if (!pool_started) {
        return -1;
    }

    struct PoolJob *job = (struct PoolJob *)malloc(sizeof(struct PoolJob));
    if (!job) {
        return -1;
    }
    job->data = data;
    job->next = NULL;

    pthread_mutex_lock(&pool_mu);
    if (pool_tail) {
        pool_tail->next = job;
    } else {
        pool_head = job;
    }
    pool_tail = job;
    pthread_cond_signal(&pool_cv);
    pthread_mutex_unlock(&pool_mu);
    return 0;
}

/* ---- plugin entry ----------------------------------------------------- */

int exec_web_request(void *scope) {
    struct WebRequestData *data = (struct WebRequestData *)getInstanceData(scope);
    char *current = getState(scope);
    did_alloc("state");

    if (!data) {
        data = (struct WebRequestData *)debug_malloc(sizeof(struct WebRequestData), "WebRequestData");
        memset(data, 0, sizeof(*data));
        atomic_init(&data->done, 0);
        atomic_init(&data->abort, 0);
        setInstanceData(scope, data);

        if (!getIntValue(scope, "Status", &data->status)) {
            log_message(scope, "WebRequest Status property is not an integer");
            changeState(scope, "Error");
            setInstanceData(scope, 0);
            debug_free(data, "WebRequestData");
            return PLUGIN_ERROR;
        }
        {
            initialisation_scope = scope;
            pthread_once(&curl_once, curl_global_init_once);
            initialisation_scope = 0;
        }
        if (!curl_initialized) {
            log_message(scope, "curl_global_init failed");
            changeState(scope, "Error");
            setInstanceData(scope, 0);
            debug_free(data, "WebRequestData");
            return PLUGIN_ERROR;
        }
    }

    if (current && strcmp(current, "Start") == 0 && !data->running) {
        char *req = getStringValue(scope, "Request");
        did_alloc("request");
        if (!req || !*req) {
            changeState(scope, "Error");
            goto done_cleanup;
        }
        data->request = req;

        char *post_data = getStringValue(scope, "PostData");
        if (post_data) {
            did_alloc("post_data");
        }
        if (post_data && *post_data && strcmp(post_data, "null") != 0) {
            data->post_data = post_data;
        } else if (post_data) {
            debug_free(post_data, "post_data");
        }

        char *content_type = getStringValue(scope, "ContentType");
        if (content_type) {
            did_alloc("content_type");
        }
        if (content_type && *content_type && strcmp(content_type, "null") != 0) {
            data->content_type = content_type;
        } else if (content_type) {
            debug_free(content_type, "content_type");
        }

        char *method = getStringValue(scope, "Method");
        if (method) {
            did_alloc("method");
        }
        if (method && *method && strcmp(method, "null") != 0) {
            data->method = method;
        } else if (method) {
            debug_free(method, "method");
        }

        char *headers_json = getStringValue(scope, "Headers");
        if (headers_json) {
            did_alloc("headers_json");
        }
        if (headers_json && *headers_json && strcmp(headers_json, "null") != 0) {
            data->headers_json = headers_json;
        } else if (headers_json) {
            debug_free(headers_json, "headers_json");
        }

        data->trust_host_certificate = getBoolValue(scope, "TrustHostCert");

        data->running = 1;
        data->http_code = 0;
        atomic_store_explicit(&data->done, 0, memory_order_relaxed);
        atomic_store_explicit(&data->abort, 0, memory_order_relaxed);

        if (!changeState(scope, "Running")) {
            goto done_cleanup;
        }

        if (enqueue_request(data) != 0) {
            setStringValue(scope, "Errors", "webrequest worker pool unavailable");
            changeState(scope, "Error");
            goto done_cleanup;
        }
        debug_free(current, "current");
        return PLUGIN_COMPLETED;
    }
    else if (current && strcmp(current, "Running") == 0 && data->running) {
        if (!atomic_load_explicit(&data->done, memory_order_acquire)) {
            debug_free(current, "current");
            return PLUGIN_COMPLETED;
        }

        /* Worker finished; no pthread_join — pool threads are long-lived. */
        setIntValue(scope, "Status", (int64_t)data->http_code);
        if (data->errors) {
            setStringValue(scope, "Errors", data->errors);
            changeState(scope, "Error");
        }
        if (data->result) {
            setJsonValue(scope, "Result", data->result ? data->result : "");
            if (!data->errors) {
                changeState(scope, "Done");
            }
        } else if (!data->errors) {
            /* Empty body success (e.g. 204) — still complete. */
            setJsonValue(scope, "Result", "");
            changeState(scope, "Done");
        }
        if (data->response_headers_json) {
            setJsonValue(scope, "ResponseHeaders", data->response_headers_json);
        }

    done_cleanup:
        if (data->request) {
            debug_free(data->request, "Request");
            data->request = 0;
        }
        if (data->post_data) {
            debug_free(data->post_data, "post_data");
            data->post_data = 0;
        }
        if (data->content_type) {
            debug_free(data->content_type, "content_type");
            data->content_type = 0;
        }
        if (data->method) {
            debug_free(data->method, "method");
            data->method = 0;
        }
        if (data->headers_json) {
            debug_free(data->headers_json, "headers_json");
            data->headers_json = 0;
        }
        if (data->result) {
            free(data->result);
            data->result = 0;
        }
        if (data->errors) {
            debug_free(data->errors, "errors");
            data->errors = 0;
        }
        if (data->response_headers_json) {
            free(data->response_headers_json);
            data->response_headers_json = 0;
        }
        setInstanceData(scope, 0);
        debug_free(data, "WebRequestData");
    }
    else if (data && data->running &&
             (!current ||
              (strcmp(current, "Running") != 0 && strcmp(current, "Start") != 0))) {
        /* Cancellation: forced out of Running (e.g. stop/reset). */
        atomic_store_explicit(&data->abort, 1, memory_order_relaxed);
        if (!atomic_load_explicit(&data->done, memory_order_acquire)) {
            debug_free(current, "current");
            return PLUGIN_COMPLETED;
        }
        setIntValue(scope, "Status", (int64_t)0L);
        setStringValue(scope, "Errors", "aborted");
        goto done_cleanup;
    }

    debug_free(current, "current");
    return PLUGIN_COMPLETED;
}
