#include "http_client.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int network_status(void) {
    u32 status = 0;
    if (R_FAILED(ACU_GetStatus(&status))) return 1;
    return (int)status;
}

void http_response_free(HttpResponse *res) {
    free(res->body);
    res->body = NULL;
    res->length = 0;
}

static int load_root(u8 **out, u32 *out_len) {
    *out = NULL;
    *out_len = 0;
    FILE *f = fopen(TWODSYNC_ROOT "/root.der", "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0 || sz > 8192) {
        fclose(f);
        return -1;
    }
    u8 *buf = malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return -1;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return -1;
    }
    fclose(f);
    *out = buf;
    *out_len = (u32)sz;
    return 0;
}

static Result prepare(httpcContext *ctx, const char *method, const char *url, const char *token, const void *body, u32 body_len, int verify_tls) {
    HTTPC_RequestMethod verb = HTTPC_METHOD_GET;
    if (strcmp(method, "POST") == 0) verb = HTTPC_METHOD_POST;
    else if (strcmp(method, "PUT") == 0) verb = HTTPC_METHOD_PUT;
    else if (strcmp(method, "DELETE") == 0) verb = HTTPC_METHOD_DELETE;
    Result rc = httpcOpenContext(ctx, verb, url, 0);
    if (R_FAILED(rc)) return rc;
    httpcSetKeepAlive(ctx, HTTPC_KEEPALIVE_DISABLED);
    if (!verify_tls) httpcSetSSLOpt(ctx, SSLCOPT_DisableVerify);
    else {
        u8 *cert = NULL;
        u32 cert_len = 0;
        if (load_root(&cert, &cert_len) == 0) {
            httpcAddTrustedRootCA(ctx, cert, cert_len);
            free(cert);
        }
    }
    httpcAddRequestHeaderField(ctx, "User-Agent", "2DSync/0.1.0");
    if (token && token[0]) {
        char header[180];
        snprintf(header, sizeof(header), "Bearer %s", token);
        httpcAddRequestHeaderField(ctx, "Authorization", header);
    }
    if (body && body_len) {
        httpcAddRequestHeaderField(ctx, "Content-Type", "application/json");
        rc = httpcAddPostDataRaw(ctx, (const u32 *)body, body_len);
        if (R_FAILED(rc)) {
            httpcCloseContext(ctx);
            return rc;
        }
    }
    return httpcBeginRequest(ctx);
}

static int read_response(httpcContext *ctx, HttpResponse *out) {
    u32 status = 0;
    Result rc = httpcGetResponseStatusCodeTimeout(ctx, &status, 15ULL * 1000 * 1000 * 1000);
    if (R_FAILED(rc)) {
        snprintf(out->error, sizeof(out->error), "No response from server (0x%08lX)", (unsigned long)rc);
        return -1;
    }
    out->status = (int)status;
    char checksum[80];
    if (R_SUCCEEDED(httpcGetResponseHeader(ctx, "X-Checksum-Sha256", checksum, sizeof(checksum)))) {
        snprintf(out->checksum, sizeof(out->checksum), "%s", checksum);
    }
    u32 downloaded = 0;
    u32 total = 0;
    httpcGetDownloadSizeState(ctx, &downloaded, &total);
    u32 cap = total ? total : 65536;
    if (cap > 65536) cap = 65536;
    u8 *buf = malloc(cap + 1);
    if (!buf) {
        snprintf(out->error, sizeof(out->error), "Out of memory");
        return -1;
    }
    u32 got = 0;
    Result dl = httpcDownloadData(ctx, buf, cap, &got);
    if (R_FAILED(dl) && dl != 0) {
        /* A short body still returns the bytes that fit. */
        if (got == 0) {
            free(buf);
            snprintf(out->error, sizeof(out->error), "Could not read server response");
            return -1;
        }
    }
    buf[got] = 0;
    out->body = (char *)buf;
    out->length = got;
    return 0;
}

static int perform(const char *method, const char *url, const char *token, const void *body, u32 body_len, int verify_tls, HttpResponse *out) {
    memset(out, 0, sizeof(*out));
    httpcContext ctx;
    Result rc = prepare(&ctx, method, url, token, body, body_len, verify_tls);
    if (R_FAILED(rc)) {
        snprintf(out->error, sizeof(out->error), "Could not open connection (0x%08lX)", (unsigned long)rc);
        return -1;
    }
    int result = read_response(&ctx, out);
    httpcCloseContext(&ctx);
    if (result != 0 && !out->error[0]) snprintf(out->error, sizeof(out->error), "Request failed");
    return result;
}

int http_json(const char *method, const char *url, const char *token, const char *json, int verify_tls, HttpResponse *out) {
    const void *body = NULL;
    u32 len = 0;
    if (json) {
        body = json;
        len = (u32)strlen(json);
    }
    return perform(method, url, token, body, len, verify_tls, out);
}

int http_upload_chunk(const char *url, const char *token, const void *data, size_t len, int verify_tls, HttpResponse *out) {
    memset(out, 0, sizeof(*out));
    httpcContext ctx;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_PUT, url, 0);
    if (R_FAILED(rc)) {
        snprintf(out->error, sizeof(out->error), "Could not open upload (0x%08lX)", (unsigned long)rc);
        return -1;
    }
    if (!verify_tls) httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "2DSync/0.1.0");
    httpcAddRequestHeaderField(&ctx, "Content-Type", "application/octet-stream");
    if (token && token[0]) {
        char header[180];
        snprintf(header, sizeof(header), "Bearer %s", token);
        httpcAddRequestHeaderField(&ctx, "Authorization", header);
    }
    rc = httpcAddPostDataRaw(&ctx, (const u32 *)data, (u32)len);
    if (R_SUCCEEDED(rc)) rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        httpcCloseContext(&ctx);
        snprintf(out->error, sizeof(out->error), "Upload failed (0x%08lX)", (unsigned long)rc);
        return -1;
    }
    int result = read_response(&ctx, out);
    httpcCloseContext(&ctx);
    return result;
}

int http_download(const char *url, const char *token, const char *path, int verify_tls, char checksum[65], void (*progress)(const char *, int)) {
    httpcContext ctx;
    Result rc = httpcOpenContext(&ctx, HTTPC_METHOD_GET, url, 0);
    if (R_FAILED(rc)) return -1;
    if (!verify_tls) httpcSetSSLOpt(&ctx, SSLCOPT_DisableVerify);
    httpcAddRequestHeaderField(&ctx, "User-Agent", "2DSync/0.1.0");
    if (token && token[0]) {
        char header[180];
        snprintf(header, sizeof(header), "Bearer %s", token);
        httpcAddRequestHeaderField(&ctx, "Authorization", header);
    }
    rc = httpcBeginRequest(&ctx);
    if (R_FAILED(rc)) {
        httpcCloseContext(&ctx);
        return -1;
    }
    u32 status = 0;
    rc = httpcGetResponseStatusCodeTimeout(&ctx, &status, 20ULL * 1000 * 1000 * 1000);
    if (R_FAILED(rc) || status != 200) {
        httpcCloseContext(&ctx);
        return -1;
    }
    if (checksum) {
        char header[80] = {0};
        if (R_SUCCEEDED(httpcGetResponseHeader(&ctx, "X-Checksum-Sha256", header, sizeof(header)))) {
            snprintf(checksum, 65, "%s", header);
        }
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.partial", path);
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        httpcCloseContext(&ctx);
        return -1;
    }
    u8 buf[4096];
    Result dl = HTTPC_RESULTCODE_DOWNLOADPENDING;
    u32 previous = 0;
    httpcGetDownloadSizeState(&ctx, &previous, NULL);
    while (dl == HTTPC_RESULTCODE_DOWNLOADPENDING) {
        u32 before = 0, total = 0;
        httpcGetDownloadSizeState(&ctx, &before, &total);
        u32 want = sizeof(buf);
        if (total > before && total - before < want) want = total - before;
        if (want == 0) break;
        dl = httpcReceiveData(&ctx, buf, want);
        u32 after = before;
        httpcGetDownloadSizeState(&ctx, &after, &total);
        if (after > before) {
            fwrite(buf, 1, after - before, out);
            if (progress && total) progress("Downloading cloud save", (int)((after * 100ULL) / total));
        } else if (dl == HTTPC_RESULTCODE_DOWNLOADPENDING) {
            break;
        }
        if (dl != HTTPC_RESULTCODE_DOWNLOADPENDING && R_FAILED(dl)) break;
        if (total && after >= total) break;
        previous = after;
    }
    fflush(out);
    fclose(out);
    httpcCloseContext(&ctx);
    remove(path);
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}
