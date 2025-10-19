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
#include <Plugin.h>
#include <curl/curl.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>

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
};

static pthread_once_t curl_once = PTHREAD_ONCE_INIT;
static int curl_initialized = 0;
static void *initialisation_scope = 0;

static void curl_global_init_once(void) {
    /* Initialize everything libcurl needs (SSL, resolver, CF proxies, etc.) */
    CURLcode rc = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (rc != CURLE_OK) {
        log_message_2(initialisation_scope, "curl_global_init failed: ", curl_easy_strerror(rc));
    }
    else {
        curl_initialized = 1;
    }
}

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct WebRequestData *data = (struct WebRequestData*)userp;
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

static void *worker(void *arg) {
    struct WebRequestData *data = (struct WebRequestData*)arg;

    /* Ensure global init happened before any easy handle is created */
    pthread_once(&curl_once, curl_global_init_once);

    CURL *curl = curl_easy_init();
    if (!curl) {
        if (!data->errors) data->errors = debug_strdup("curl_easy_init failed", "errors");
        data->done = 1;
        return NULL;
    }

    curl_easy_setopt(curl, CURLOPT_URL, data->request);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, data);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ClockworkWebRequest/1.0");
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L); /* good hygiene on macOS */
    if (data->trust_host_certificate) {
        // Disable verification of the server's certificate
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        // Disable verification of the host name in the certificate
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
    }

    if (data->post_data && *data->post_data) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data->post_data);
    }

    /* If you want to *avoid* macOS proxy auto-detection entirely, uncomment:
       curl_easy_setopt(curl, CURLOPT_PROXY, "");
       // or: curl_easy_setopt(curl, CURLOPT_NOPROXY, "*");
    */

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        if (!data->errors) data->errors = debug_strdup(curl_easy_strerror(res), "errors");
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &data->http_code);
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
            initialisation_scope = scope; // used to log a startup error
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
        did_alloc("post_data");
        if (post_data && *post_data && strcmp(post_data, "null") != 0) {
            data->post_data = post_data;
        } else if (post_data) {
            debug_free(post_data, "post_data");
        }

        data->trust_host_certificate = getBoolValue(scope, "TrustHostCert");

        data->running = 1;
        data->done = 0;

        if (!changeState(scope, "Running")) goto done_cleanup;

        /* Spawn worker thread */
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
            return PLUGIN_COMPLETED; /* still running */
        }

        /* Join and finalize */
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
        if (data->request) { debug_free(data->request, "Request"); data->request = 0; }
        if (data->post_data) { debug_free(data->post_data, "post_data"); data->post_data = 0; }
        if (data->result) { free(data->result); data->result = 0; }
        if (data->errors) { free(data->errors); data->errors = 0; }
        setInstanceData(scope, 0);
        debug_free(data, "WebRequestData");
    }

    debug_free(current, "current");
    return PLUGIN_COMPLETED;
}
