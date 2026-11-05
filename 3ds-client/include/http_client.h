#pragma once

#include <stddef.h>

typedef struct HttpResponse {
    int status;
    char *body;
    size_t length;
    char checksum[80];
    char error[160];
} HttpResponse;

int network_status(void);
void http_response_free(HttpResponse *res);
int http_json(const char *method, const char *url, const char *token, const char *json, int verify_tls, HttpResponse *out);
int http_download(const char *url, const char *token, const char *path, int verify_tls, char checksum[65], void (*progress)(const char *, int));
int http_upload_chunk(const char *url, const char *token, const void *data, size_t len, int verify_tls, HttpResponse *out);
