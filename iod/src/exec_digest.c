#include <Plugin.h>
#include <debug_malloc.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "exec_digest.h"

static const EVP_MD *allowed_digest(const char *algorithm) {
    if (algorithm && strcasecmp(algorithm, "SHA512") == 0) {
        return EVP_sha512();
    }
    if (algorithm && strcasecmp(algorithm, "SHA256") == 0) {
        return EVP_sha256();
    }
    return NULL;
}

static int digest_hex(const EVP_MD *digest_type,
                      const char *input,
                      char *output,
                      size_t output_size) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_length = 0;
    EVP_MD_CTX *context = NULL;

    if (!digest_type || !input || !output || output_size < EVP_MAX_MD_SIZE * 2 + 1) {
        return 0;
    }

    context = EVP_MD_CTX_new();
    if (!context) {
        return 0;
    }

    int ok = EVP_DigestInit_ex(context, digest_type, NULL) == 1 &&
             EVP_DigestUpdate(context, input, strlen(input)) == 1 &&
             EVP_DigestFinal_ex(context, digest, &digest_length) == 1;
    EVP_MD_CTX_free(context);
    if (!ok) {
        return 0;
    }

    for (unsigned int i = 0; i < digest_length; ++i) {
        snprintf(output + i * 2, output_size - i * 2, "%02x", digest[i]);
    }
    output[digest_length * 2] = '\0';
    return 1;
}

int exec_digest(void *scope) {
    char *state = getState(scope);
    if (!state) {
        return PLUGIN_ERROR;
    }
    did_alloc("state");

    if (strcmp(state, "Start") == 0) {
        char *algorithm = getStringValue(scope, "Algorithm");
        char *input = getStringValue(scope, "Input");
        if (algorithm) did_alloc("digest_algorithm");
        if (input) did_alloc("digest_input");

        char result[EVP_MAX_MD_SIZE * 2 + 1];
        const EVP_MD *digest_type = allowed_digest(algorithm);
        if (!digest_type) {
            setStringValue(scope, "Errors", "Unsupported digest algorithm");
            changeState(scope, "Error");
        } else if (!digest_hex(digest_type, input, result, sizeof(result))) {
            setStringValue(scope, "Errors", "Digest calculation failed");
            changeState(scope, "Error");
        } else {
            setStringValue(scope, "Result", result);
            setStringValue(scope, "Errors", "");
            changeState(scope, "Done");
        }

        if (algorithm) debug_free(algorithm, "digest_algorithm");
        if (input) debug_free(input, "digest_input");
    }

    debug_free(state, "state");
    return PLUGIN_COMPLETED;
}
