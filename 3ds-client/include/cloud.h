#pragma once

#include "config.h"

#include <stddef.h>

typedef struct CloudSaveInfo {
    int found;
    char version_id[40];
    char checksum[65];
    char content_checksum[65];
    char created_at[40];
    unsigned long long bytes;
} CloudSaveInfo;

typedef struct RemoteDevice {
    char id[40];
    char name[32];
    int this_console;
    int revoked;
} RemoteDevice;

typedef struct PairPending {
    char id[40];
    char name[32];
} PairPending;

int cloud_check(const AppConfig *cfg, char *err, size_t err_len);
int cloud_create_account(AppConfig *cfg, char *recovery, size_t recovery_len, char *err, size_t err_len);
int cloud_create_sync_code(const AppConfig *cfg, char *code, size_t code_len, int *expires, char *err, size_t err_len);
int cloud_redeem_code(AppConfig *cfg, const char *code, char *request_id, size_t request_len, char *poll_token, size_t poll_len, char *err, size_t err_len);
int cloud_poll(AppConfig *cfg, const char *request_id, const char *poll_token, char *status, size_t status_len, char *err, size_t err_len);
int cloud_list_pending(const AppConfig *cfg, PairPending *items, int max_items, char *err, size_t err_len);
int cloud_decide(const AppConfig *cfg, const char *request_id, int approve, char *err, size_t err_len);
int cloud_recover(AppConfig *cfg, const char *recovery_code, char *new_recovery, size_t new_len, char *err, size_t err_len);
int cloud_rotate_recovery(const AppConfig *cfg, char *recovery, size_t recovery_len, char *err, size_t err_len);
int cloud_list_devices(const AppConfig *cfg, RemoteDevice *items, int max_items, char *err, size_t err_len);
int cloud_revoke(const AppConfig *cfg, const char *device_id, int self, char *err, size_t err_len);
int cloud_latest(const AppConfig *cfg, const char *title_hex, CloudSaveInfo *info, char *err, size_t err_len);
int cloud_upload_container(const AppConfig *cfg, const char *title_hex, const char *path, const char *container_sha, const char *content_sha, CloudSaveInfo *info, char *err, size_t err_len, void (*progress)(const char *, int));
int cloud_download_version(const AppConfig *cfg, const char *title_hex, const char *version_id, const char *dest, char *err, size_t err_len, void (*progress)(const char *, int));

typedef struct CloudListItem {
    char title_id[20];
    char created_at[40];
    unsigned long long bytes;
} CloudListItem;

int cloud_list_saves(const AppConfig *cfg, CloudListItem *items, int max_items, char *err, size_t err_len);
