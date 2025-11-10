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
#include "exec_web_request.h"
#include "stdio.h"
#include "assert.h"
#include <pthread.h>

struct WebRequestData {
    pthread_t thread;
    int running;
    int done;
    long http_code;
    char *request;
    char *post_data;
    char *result;
    char *errors;
    const int64_t *status;
    int trust_host_certificate;
    /* Optional: Content-Type header value */
    char *content_type;
    /* NEW: Optional HTTP method override (e.g., PATCH/PUT/DELETE/HEAD/GET/POST) */
    char *method;
    int abort;
};

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
    struct WebRequestData *data = (struct WebRequestData*)userp;
    if (data->abort) {
        return 0;
    }
    size_t old_len = data->result ? strlen(data->result) : 0;
    char *new_buf = 0;
    if (data->result) {
        new_buf = (char*)realloc(data->result, old_len + realsize + 1);
    } else {
        new_buf = (char*)malloc(realsize + 1);
    }
    if (!new_buf) {
        return 0;
    }
    memcpy(new_buf + old_len, contents, realsize);
    new_buf[old_len + realsize] = '\0';
    data->result = new_buf;
    return realsize;
}

static int xferinfo_cb(void *clientp,
                       curl_off_t dltotal, curl_off_t dlnow,
                       curl_off_t ultotal, curl_off_t ulnow) {
    struct WebRequestData *data = (struct WebRequestData *)clientp;
    if (data && data->abort) {
        printf("aborting\n");
        /* Returning non-zero tells libcurl to abort the transfer. */
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
    if (data && data->abort) {
        printf("aborting (progress)\n");
        return 1;
    }
    (void)dltotal;
    (void)dlnow;
    (void)ultotal;
    (void)ulnow;
    return 0;
}

static void *worker(void *arg) {
    struct WebRequestData *data = (struct WebRequestData*)arg;

    pthread_once(&curl_once, curl_global_init_once);

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (!data->errors) data->errors = debug_strdup("curl_easy_init failed", "errors");
        data->done = 1;
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, data);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, xferinfo_cb);
    curl_easy_setopt(curl, CURLOPT_PROGRESSDATA, data);
    curl_easy_setopt(curl, CURLOPT_PROGRESSFUNCTION, progress_cb);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);

    /* Optional headers list (for Content-Type or others later) */
    struct curl_slist *headers = NULL;

    curl_easy_setopt(curl, CURLOPT_URL, data->request);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ClockworkWebRequest/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    if (data->trust_host_certificate) {
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    /* Set Content-Type if provided */
    if (data->content_type && *data->content_type) {
        const char *prefix = "Content-Type: ";
        size_t need = strlen(prefix) + strlen(data->content_type) + 1;
        char *ct_header = (char*)malloc(need);
        if (ct_header) {
            memcpy(ct_header, prefix, strlen(prefix));
            memcpy(ct_header + strlen(prefix), data->content_type, strlen(data->content_type) + 1);
            headers = curl_slist_append(headers, ct_header);
            free(ct_header);
            if (headers) {
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            } else if (!data->errors) {
                data->errors = debug_strdup("Failed to append Content-Type header", "errors");
            }
        } else if (!data->errors) {
            data->errors = debug_strdup("malloc failed building Content-Type header", "errors");
        }
    }

    /* Determine HTTP method: default behavior vs override */
    const char *method = (data->method && *data->method) ? data->method : NULL;
    if (method) {
        /* Case-insensitive comparisons for convenience */
        if (strcasecmp(method, "GET") == 0) {
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
        } else if (strcasecmp(method, "POST") == 0) {
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
        } else if (strcasecmp(method, "HEAD") == 0) {
            curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "HEAD");
        } else {
            /* PATCH, PUT, DELETE, OPTIONS, etc. */
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method);
        }
    }

    /* Attach body if provided.
       - Default (no method override): this will become POST if PostData is present.
       - With override:
           * POST: we also set CURLOPT_POST (above), so this is a POST with body.
           * PATCH/PUT/DELETE/etc: we set CUSTOMREQUEST above; POSTFIELDS here supplies the body.
           * HEAD: ignore any PostData. */
    if (data->post_data && *data->post_data) {
        if (!method) {
            /* preserve original default: body => POST */
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data->post_data);
        } else if (strcasecmp(method, "HEAD") == 0) {
            /* HEAD must not have a body; do nothing */
        } else {
            /* For POST, PATCH, PUT, DELETE, etc., send body */
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data->post_data);
        }
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (!data->errors) data->errors = debug_strdup(curl_easy_strerror(res), "errors");
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &data->http_code);

    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    data->done = 1;
    return NULL;
}

int exec_web_request(void *scope) {
    struct WebRequestData *data = (struct WebRequestData *)getInstanceData(scope);
    char *current = getState(scope);
    did_alloc("state");

    if (!data) {
        data = (struct WebRequestData*)debug_malloc(sizeof(struct WebRequestData), "WebRequestData");
        memset(data, 0, sizeof(*data));
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

        /* Optional POST body */
        char *post_data = getStringValue(scope, "PostData");
        if (post_data) { did_alloc("post_data"); }
        if (post_data && *post_data && strcmp(post_data, "null") != 0) {
            data->post_data = post_data;
        } else if (post_data) {
            debug_free(post_data, "post_data");
        }

        /* Optional Content-Type */
        char *content_type = getStringValue(scope, "ContentType");
        if (content_type) { did_alloc("content_type"); }
        if (content_type && *content_type && strcmp(content_type, "null") != 0) {
            data->content_type = content_type;
        } else if (content_type) {
            debug_free(content_type, "content_type");
        }

        /* NEW: Optional Method override */
        char *method = getStringValue(scope, "Method");
        if (method) { did_alloc("method"); }
        if (method && *method && strcmp(method, "null") != 0) {
            data->method = method;
        } else if (method) {
            debug_free(method, "method");
        }

        data->trust_host_certificate = getBoolValue(scope, "TrustHostCert");

        data->running = 1;
        data->done = 0;
        data->abort = 0;

        if (!changeState(scope, "Running")) goto done_cleanup;

        int rc = pthread_create(&data->thread, NULL, worker, data);
        if (rc != 0) {
            setStringValue(scope, "Errors", "pthread_create failed");
            changeState(scope, "Error");
            goto done_cleanup;
        }
        debug_free(current, "current");
        return PLUGIN_COMPLETED;
    }
    else if (current && strcmp(current, "Running") == 0 && data->running) {
        if (!data->done) {
            debug_free(current, "current");
            return PLUGIN_COMPLETED;
        }

        pthread_join(data->thread, NULL);

        setIntValue(scope, "Status", (int64_t)data->http_code);
        if (data->errors) {
            setStringValue(scope, "Errors", data->errors);
            changeState(scope, "Error");
        }
        if (data->result) {
            setJsonValue(scope, "Result", data->result ? data->result : "");
            if (!data->errors) { changeState(scope, "Done"); }
        }

    done_cleanup:
        if (data->request)      { debug_free(data->request, "Request"); data->request = 0; }
        if (data->post_data)    { debug_free(data->post_data, "post_data"); data->post_data = 0; }
        if (data->content_type) { debug_free(data->content_type, "content_type"); data->content_type = 0; }
        if (data->method)       { debug_free(data->method, "method"); data->method = 0; }
        if (data->result)       { free(data->result); data->result = 0; }
        if (data->errors)       { free(data->errors); data->errors = 0; }
        setInstanceData(scope, 0);
        debug_free(data, "WebRequestData");
    }
    else if (data && data->running && (!current
                                   || (strcmp(current, "Running") != 0 && strcmp(current, "Start") != 0))) {
        /* Cancellation path: thread is still running, but the state is no longer "Running".
           This happens when a Clockwork command (e.g., stop) moves the machine back to Idle. */
        if (!data->abort) {
            data->abort = 1;
        }
        if (!data->done) {
            /* Wait for the worker to notice the abort and finish. */
            debug_free(current, "current");
            return PLUGIN_COMPLETED;
        }
        /* Thread is finished, join and clean up, but do not update Result/Status/Errors
           or change the state (Clockwork has already set it, typically to Idle). */
        pthread_join(data->thread, NULL);
        setIntValue(scope, "Status", (int64_t)0L);
        setStringValue(scope, "Errors", "aborted");
        goto done_cleanup;
    }

    debug_free(current, "current");
    return PLUGIN_COMPLETED;
}
