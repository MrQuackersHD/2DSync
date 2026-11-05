#pragma once

#include "save_fs.h"

#define MAX_BACKUPS 24

typedef struct LocalBackup {
    char path[384];
    char label[48];
    u64 unix_time;
    u64 bytes;
    int has_secure;
    char content_sha[65];
    char container_sha[65];
} LocalBackup;

int backup_create(const TitleEntry *title, char *out_path, size_t out_len, char content_sha[65], char container_sha[65], char *err, size_t err_len, ProgressFn progress);
int backup_list(u64 title_id, LocalBackup *items, int max_items);
int backup_restore(const TitleEntry *title, const char *container_path, char *err, size_t err_len, ProgressFn progress);
int backup_stage_hash(const TitleEntry *title, char *staged_dir, size_t staged_len, char content_sha[65], char container_sha[65], SaveSnapshot *snap, char *err, size_t err_len, ProgressFn progress);
