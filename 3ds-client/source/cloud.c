#include "cloud.h"
#include "http_client.h"
#include "json_mini.h"
#include "net_policy.h"
#include "sha256.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int gate(const AppConfig *cfg, char *err, size_t err_len, int need_token) {
    int code = cloud_op_allowed(network_status(), config_has_server(cfg), need_token ? config_has_token(cfg) : 1);
    if (code != NET_OK) {
        snprintf(err, err_len, "%s", net_policy_message(code));
        return -1;
    }
    return 0;
}

static int make_url(const AppConfig *cfg, char *out, size_t out_len, const char *path) {
    const char *base = cfg->server_url;
    size_t n = strlen(base);
    while (n && base[n - 1] == '/') n--;
    int wrote = snprintf(out, out_len, "%.*s%s", (int)n, base, path);
    return wrote > 0 && (size_t)wrote < out_len;
}

static int call_json(const AppConfig *cfg, const char *method, const char *path, const char *json, int authed, HttpResponse *res, char *err, size_t err_len) {
    char url[384];
    if (!make_url(cfg, url, sizeof(url), path)) {
        snprintf(err, err_len, "Server address is too long");
        return -1;
    }
    if (http_json(method, url, authed ? cfg->device_token : NULL, json, cfg->verify_tls, res) != 0) {
        snprintf(err, err_len, "%s", res->error[0] ? res->error : "Network request failed");
        http_response_free(res);
        return -1;
    }
    if (res->status >= 400) {
        char detail[140];
        if (json_get_string(res->body ? res->body : "", "detail", detail, sizeof(detail)) != 0) snprintf(detail, sizeof(detail), "Server returned %d", res->status);
        snprintf(err, err_len, "%s", detail);
        http_response_free(res);
        return -1;
    }
    return 0;
}

static void take_device(AppConfig *cfg, const char *json) {
    json_get_string(json, "account_id", cfg->account_id, sizeof(cfg->account_id));
    json_get_string(json, "device_id", cfg->device_id, sizeof(cfg->device_id));
    json_get_string(json, "token", cfg->device_token, sizeof(cfg->device_token));
}

int cloud_check(const AppConfig *cfg, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 0) != 0) return -1;
    HttpResponse res;
    if (call_json(cfg, "GET", "/health", NULL, 0, &res, err, err_len) != 0) return -1;
    http_response_free(&res);
    snprintf(err, err_len, "Server is reachable");
    return 0;
}

int cloud_create_account(AppConfig *cfg, char *recovery, size_t recovery_len, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 0) != 0) return -1;
    char body[192];
    snprintf(body, sizeof(body), "{\"console_name\":\"%.30s\",\"console_id\":\"%s\"}", cfg->console_name, cfg->console_id);
    HttpResponse res;
    if (call_json(cfg, "POST", "/v1/accounts", body, 0, &res, err, err_len) != 0) return -1;
    take_device(cfg, res.body);
    json_get_string(res.body, "recovery_code", recovery, recovery_len);
    http_response_free(&res);
    if (!cfg->device_token[0] || !recovery[0]) {
        snprintf(err, err_len, "Account response was incomplete");
        return -1;
    }
    config_save(cfg);
    return 0;
}

int cloud_create_sync_code(const AppConfig *cfg, char *code, size_t code_len, int *expires, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    HttpResponse res;
    if (call_json(cfg, "POST", "/v1/pairing/codes", "{}", 1, &res, err, err_len) != 0) return -1;
    json_get_string(res.body, "code", code, code_len);
    unsigned long long exp = 0;
    json_get_u64(res.body, "expires_in", &exp);
    if (expires) *expires = (int)exp;
    http_response_free(&res);
    return code[0] ? 0 : -1;
}

int cloud_redeem_code(AppConfig *cfg, const char *code, char *request_id, size_t request_len, char *poll_token, size_t poll_len, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 0) != 0) return -1;
    char body[256];
    snprintf(body, sizeof(body), "{\"code\":\"%.16s\",\"console_name\":\"%.30s\",\"console_id\":\"%s\"}", code, cfg->console_name, cfg->console_id);
    HttpResponse res;
    if (call_json(cfg, "POST", "/v1/pairing/redeem", body, 0, &res, err, err_len) != 0) return -1;
    json_get_string(res.body, "request_id", request_id, request_len);
    json_get_string(res.body, "poll_token", poll_token, poll_len);
    http_response_free(&res);
    return request_id[0] ? 0 : -1;
}

int cloud_poll(AppConfig *cfg, const char *request_id, const char *poll_token, char *status, size_t status_len, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 0) != 0) return -1;
    char path[256];
    snprintf(path, sizeof(path), "/v1/pairing/requests/%s?poll_token=%s", request_id, poll_token);
    HttpResponse res;
    if (call_json(cfg, "GET", path, NULL, 0, &res, err, err_len) != 0) return -1;
    json_get_string(res.body, "status", status, status_len);
    if (strcmp(status, "approved") == 0) {
        take_device(cfg, res.body);
        config_save(cfg);
        char ack[256];
        snprintf(ack, sizeof(ack), "/v1/pairing/requests/%s/ack?poll_token=%s", request_id, poll_token);
        HttpResponse done;
        call_json(cfg, "POST", ack, "{}", 0, &done, err, err_len);
        http_response_free(&done);
    }
    http_response_free(&res);
    return 0;
}

int cloud_list_pending(const AppConfig *cfg, PairPending *items, int max_items, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    HttpResponse res;
    if (call_json(cfg, "GET", "/v1/pairing/requests", NULL, 1, &res, err, err_len) != 0) return -1;
    int count = 0;
    for (int i = 0; i < max_items; i++) {
        const char *start = NULL;
        const char *end = NULL;
        if (json_object_at(res.body, "requests", i, &start, &end) != 0) break;
        char obj[256];
        size_t n = (size_t)(end - start);
        if (n >= sizeof(obj)) n = sizeof(obj) - 1;
        memcpy(obj, start, n);
        obj[n] = 0;
        json_get_string(obj, "request_id", items[count].id, sizeof(items[count].id));
        json_get_string(obj, "console_name", items[count].name, sizeof(items[count].name));
        count++;
    }
    http_response_free(&res);
    return count;
}

int cloud_decide(const AppConfig *cfg, const char *request_id, int approve, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    char path[192];
    snprintf(path, sizeof(path), "/v1/pairing/requests/%s/%s", request_id, approve ? "approve" : "deny");
    HttpResponse res;
    int rc = call_json(cfg, "POST", path, "{}", 1, &res, err, err_len);
    if (rc == 0) http_response_free(&res);
    return rc;
}

int cloud_recover(AppConfig *cfg, const char *recovery_code, char *new_recovery, size_t new_len, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 0) != 0) return -1;
    char body[256];
    snprintf(body, sizeof(body), "{\"recovery_code\":\"%.20s\",\"console_name\":\"%.30s\",\"console_id\":\"%s\"}", recovery_code, cfg->console_name, cfg->console_id);
    HttpResponse res;
    if (call_json(cfg, "POST", "/v1/recovery/redeem", body, 0, &res, err, err_len) != 0) return -1;
    take_device(cfg, res.body);
    json_get_string(res.body, "recovery_code", new_recovery, new_len);
    http_response_free(&res);
    config_save(cfg);
    return cfg->device_token[0] ? 0 : -1;
}

int cloud_rotate_recovery(const AppConfig *cfg, char *recovery, size_t recovery_len, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    HttpResponse res;
    if (call_json(cfg, "POST", "/v1/recovery/rotate", "{}", 1, &res, err, err_len) != 0) return -1;
    json_get_string(res.body, "recovery_code", recovery, recovery_len);
    http_response_free(&res);
    return recovery[0] ? 0 : -1;
}

int cloud_list_devices(const AppConfig *cfg, RemoteDevice *items, int max_items, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    HttpResponse res;
    if (call_json(cfg, "GET", "/v1/devices", NULL, 1, &res, err, err_len) != 0) return -1;
    int count = 0;
    for (int i = 0; i < max_items; i++) {
        const char *start = NULL;
        const char *end = NULL;
        if (json_object_at(res.body, "devices", i, &start, &end) != 0) break;
        char obj[320];
        size_t n = (size_t)(end - start);
        if (n >= sizeof(obj)) n = sizeof(obj) - 1;
        memcpy(obj, start, n);
        obj[n] = 0;
        json_get_string(obj, "device_id", items[count].id, sizeof(items[count].id));
        json_get_string(obj, "name", items[count].name, sizeof(items[count].name));
        items[count].this_console = strstr(obj, "\"this_console\":true") != NULL || strstr(obj, "\"this_console\": true") != NULL;
        items[count].revoked = strstr(obj, "\"revoked\":true") != NULL || strstr(obj, "\"revoked\": true") != NULL;
        if (!items[count].revoked) count++;
    }
    http_response_free(&res);
    return count;
}

int cloud_revoke(const AppConfig *cfg, const char *device_id, int self, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    char path[128];
    snprintf(path, sizeof(path), "%s", self ? "/v1/devices/me" : "/v1/devices/");
    char full[180];
    if (self) snprintf(full, sizeof(full), "%s", path);
    else snprintf(full, sizeof(full), "/v1/devices/%s", device_id);
    HttpResponse res;
    int rc = call_json(cfg, "DELETE", full, NULL, 1, &res, err, err_len);
    if (rc == 0) http_response_free(&res);
    return rc;
}

static void fill_save(CloudSaveInfo *info, const char *json) {
    memset(info, 0, sizeof(*info));
    info->found = 1;
    json_get_string(json, "id", info->version_id, sizeof(info->version_id));
    json_get_string(json, "checksum", info->checksum, sizeof(info->checksum));
    json_get_string(json, "content_checksum", info->content_checksum, sizeof(info->content_checksum));
    json_get_string(json, "created_at", info->created_at, sizeof(info->created_at));
    json_get_u64(json, "byte_size", &info->bytes);
}

int cloud_latest(const AppConfig *cfg, const char *title_hex, CloudSaveInfo *info, char *err, size_t err_len) {
    memset(info, 0, sizeof(*info));
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    char path[80];
    snprintf(path, sizeof(path), "/v1/saves/%s", title_hex);
    char url[384];
    make_url(cfg, url, sizeof(url), path);
    HttpResponse res;
    if (http_json("GET", url, cfg->device_token, NULL, cfg->verify_tls, &res) != 0) {
        snprintf(err, err_len, "%s", res.error[0] ? res.error : "Could not reach the server");
        http_response_free(&res);
        return -1;
    }
    if (res.status == 404) {
        http_response_free(&res);
        info->found = 0;
        return 0;
    }
    if (res.status >= 400) {
        char detail[140];
        if (json_get_string(res.body ? res.body : "", "detail", detail, sizeof(detail)) != 0) snprintf(detail, sizeof(detail), "Server returned %d", res.status);
        snprintf(err, err_len, "%s", detail);
        http_response_free(&res);
        return -1;
    }
    fill_save(info, res.body ? res.body : "");
    http_response_free(&res);
    return 0;
}

int cloud_upload_container(const AppConfig *cfg, const char *title_hex, const char *path, const char *container_sha, const char *content_sha, CloudSaveInfo *info, char *err, size_t err_len, void (*progress)(const char *, int)) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    FILE *f = fopen(path, "rb");
    if (!f) {
        snprintf(err, err_len, "Could not read the save container");
        return -1;
    }
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0) {
        fclose(f);
        snprintf(err, err_len, "Save container is empty");
        return -1;
    }
    char body[256];
    snprintf(body, sizeof(body), "{\"byte_size\":%ld,\"checksum_sha256\":\"%s\",\"content_sha256\":\"%s\"}", size, container_sha, content_sha);
    char api[96];
    snprintf(api, sizeof(api), "/v1/saves/%s/uploads", title_hex);
    HttpResponse res;
    if (call_json(cfg, "POST", api, body, 1, &res, err, err_len) != 0) {
        fclose(f);
        return -1;
    }
    char upload_id[40];
    json_get_string(res.body, "upload_id", upload_id, sizeof(upload_id));
    unsigned long long chunk = 32768;
    json_get_u64(res.body, "chunk_size", &chunk);
    if (chunk > 24576) chunk = 24576;
    http_response_free(&res);
    long offset = 0;
    u8 *buf = malloc((size_t)chunk);
    if (!buf) {
        fclose(f);
        snprintf(err, err_len, "Out of memory");
        return -1;
    }
    while (offset < size) {
        size_t n = fread(buf, 1, (size_t)chunk, f);
        if (n == 0) break;
        char url[384];
        char path_url[160];
        snprintf(path_url, sizeof(path_url), "/v1/uploads/%s/chunks?offset=%ld", upload_id, offset);
        make_url(cfg, url, sizeof(url), path_url);
        HttpResponse part;
        if (http_upload_chunk(url, cfg->device_token, buf, n, cfg->verify_tls, &part) != 0 || part.status >= 400) {
            snprintf(err, err_len, "%s", part.error[0] ? part.error : "Upload interrupted. Local save was not changed.");
            char detail[120];
            if (part.body && json_get_string(part.body, "detail", detail, sizeof(detail)) == 0) snprintf(err, err_len, "%s", detail);
            http_response_free(&part);
            free(buf);
            fclose(f);
            return -1;
        }
        http_response_free(&part);
        offset += (long)n;
        if (progress) progress("Uploading save", (int)((offset * 100L) / size));
    }
    free(buf);
    fclose(f);
    if (offset != size) {
        snprintf(err, err_len, "Upload interrupted. Local save was not changed.");
        return -1;
    }
    char commit[128];
    snprintf(commit, sizeof(commit), "/v1/uploads/%s/commit", upload_id);
    if (call_json(cfg, "POST", commit, "{}", 1, &res, err, err_len) != 0) return -1;
    fill_save(info, res.body ? res.body : "");
    http_response_free(&res);
    return 0;
}

int cloud_list_saves(const AppConfig *cfg, CloudListItem *items, int max_items, char *err, size_t err_len) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    HttpResponse res;
    if (call_json(cfg, "GET", "/v1/saves", NULL, 1, &res, err, err_len) != 0) return -1;
    int count = 0;
    for (int i = 0; i < max_items; i++) {
        const char *start = NULL;
        const char *end = NULL;
        if (json_object_at(res.body, "saves", i, &start, &end) != 0) break;
        char obj[384];
        size_t n = (size_t)(end - start);
        if (n >= sizeof(obj)) n = sizeof(obj) - 1;
        memcpy(obj, start, n);
        obj[n] = 0;
        json_get_string(obj, "title_id", items[count].title_id, sizeof(items[count].title_id));
        json_get_string(obj, "created_at", items[count].created_at, sizeof(items[count].created_at));
        json_get_u64(obj, "byte_size", &items[count].bytes);
        count++;
    }
    http_response_free(&res);
    return count;
}

int cloud_download_version(const AppConfig *cfg, const char *title_hex, const char *version_id, const char *dest, char *err, size_t err_len, void (*progress)(const char *, int)) {
    if (gate(cfg, err, err_len, 1) != 0) return -1;
    char path[180];
    snprintf(path, sizeof(path), "/v1/saves/%s/versions/%s/download", title_hex, version_id);
    char url[420];
    make_url(cfg, url, sizeof(url), path);
    char checksum[65] = {0};
    if (http_download(url, cfg->device_token, dest, cfg->verify_tls, checksum, progress) != 0) {
        snprintf(err, err_len, "Download failed. Existing save was not changed.");
        return -1;
    }
    uint8_t dig[32];
    char actual[65];
    if (sha256_file(dest, dig) != 0) {
        snprintf(err, err_len, "Could not verify the download");
        remove(dest);
        return -1;
    }
    sha256_hex(dig, actual);
    if (checksum[0] && strcmp(checksum, actual) != 0) {
        snprintf(err, err_len, "Downloaded save failed checksum verification");
        remove(dest);
        return -1;
    }
    return 0;
}
