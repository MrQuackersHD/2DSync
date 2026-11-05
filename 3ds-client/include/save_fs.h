#pragma once

#include "titles.h"
#include "util.h"

#include <stdint.h>

typedef struct SaveSnapshot {
    int has_secure;
    uint64_t secure_value;
    int file_count;
} SaveSnapshot;

int save_export_to(const TitleEntry *title, const char *dir, SaveSnapshot *snap, char *err, size_t err_len, ProgressFn progress);
int save_import_from(const TitleEntry *title, const char *dir, const SaveSnapshot *snap, char *err, size_t err_len, ProgressFn progress);
