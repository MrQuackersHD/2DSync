#include "backup.h"
#include "container.h"
#include "sha256.h"
#include "titles.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static void stage_paths(u64 title_id, char *work, size_t work_len, char *dir, size_t dir_len) {
    char id[17];
    format_title_id(id, title_id);
    snprintf(work, work_len, TWODSYNC_ROOT "/tmp/%s", id);
    snprintf(dir, dir_len, TWODSYNC_ROOT "/backups/%s", id);
    mkdir_p(work);
    mkdir_p(dir);
}

int backup_stage_hash(const TitleEntry *title, char *staged_dir, size_t staged_len, char content_sha[65], char container_sha[65], SaveSnapshot *snap, char *err, size_t err_len, ProgressFn progress) {
    char work[384], backup_dir[384];
    stage_paths(title->title_id, work, sizeof(work), backup_dir, sizeof(backup_dir));
    snprintf(staged_dir, staged_len, "%s", work);
    if (save_export_to(title, work, snap, err, err_len, progress) != 0) return -1;
    if (container_content_hash(work, content_sha, err, (unsigned)err_len) != 0) return -1;
    char packed[512];
    snprintf(packed, sizeof(packed), "%s/current.2dsv", work);
    if (container_pack_dir(work, packed, title->title_id, snap->has_secure, snap->secure_value, unix_time(), container_sha, err, (unsigned)err_len) != 0) return -1;
    return 0;
}

int backup_create(const TitleEntry *title, char *out_path, size_t out_len, char content_sha[65], char container_sha[65], char *err, size_t err_len, ProgressFn progress) {
    char staged[384];
    SaveSnapshot snap;
    if (backup_stage_hash(title, staged, sizeof(staged), content_sha, container_sha, &snap, err, err_len, progress) != 0) return -1;
    char id[17];
    format_title_id(id, title->title_id);
    u64 now = unix_time();
    snprintf(out_path, out_len, TWODSYNC_ROOT "/backups/%s/%llu.2dsv", id, (unsigned long long)now);
    char src[512];
    snprintf(src, sizeof(src), "%s/current.2dsv", staged);
    FILE *in = fopen(src, "rb");
    FILE *out = fopen(out_path, "wb");
    if (!in || !out) {
        if (in) fclose(in);
        if (out) fclose(out);
        snprintf(err, err_len, "Could not store the local backup");
        return -1;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fflush(out);
    fclose(out);
    if (progress) progress("Local backup stored", 100);
    return 0;
}

int backup_list(u64 title_id, LocalBackup *items, int max_items) {
    char id[17];
    format_title_id(id, title_id);
    char dirpath[384];
    snprintf(dirpath, sizeof(dirpath), TWODSYNC_ROOT "/backups/%s", id);
    DIR *dir = opendir(dirpath);
    if (!dir) return 0;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL && count < max_items) {
        size_t len = strlen(de->d_name);
        if (len < 6 || strcmp(de->d_name + len - 5, ".2dsv") != 0) continue;
        LocalBackup *item = &items[count];
        memset(item, 0, sizeof(*item));
        snprintf(item->path, sizeof(item->path), "%s/%s", dirpath, de->d_name);
        ContainerInfo info;
        char err[64];
        if (container_read_info(item->path, &info, err, sizeof(err)) != 0) continue;
        item->unix_time = info.unix_time;
        item->has_secure = info.has_secure_value;
        struct stat st;
        if (stat(item->path, &st) == 0) item->bytes = (u64)st.st_size;
        uint8_t dig[32];
        if (sha256_file(item->path, dig) == 0) sha256_hex(dig, item->container_sha);
        format_time(item->label, sizeof(item->label), item->unix_time);
        count++;
    }
    closedir(dir);
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (items[j].unix_time > items[i].unix_time) {
                LocalBackup tmp = items[i];
                items[i] = items[j];
                items[j] = tmp;
            }
        }
    }
    return count;
}

int backup_catalog(BackupCatalogItem *items, int max_items, const TitleList *titles) {
    DIR *root = opendir(TWODSYNC_ROOT "/backups");
    if (!root) return 0;
    int count = 0;
    struct dirent *de;
    while ((de = readdir(root)) != NULL && count < max_items) {
        if (de->d_name[0] == '.') continue;
        char dirpath[384];
        snprintf(dirpath, sizeof(dirpath), TWODSYNC_ROOT "/backups/%s", de->d_name);
        struct stat st;
        if (stat(dirpath, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
        u64 title_id = 0;
        if (sscanf(de->d_name, "%16llx", (unsigned long long *)&title_id) != 1) continue;
        LocalBackup per_title[MAX_BACKUPS];
        int n = backup_list(title_id, per_title, MAX_BACKUPS);
        for (int i = 0; i < n && count < max_items; i++) {
            BackupCatalogItem *item = &items[count];
            memset(item, 0, sizeof(*item));
            item->title_id = title_id;
            titles_lookup_name(titles, title_id, item->name, sizeof(item->name));
            snprintf(item->path, sizeof(item->path), "%s", per_title[i].path);
            snprintf(item->label, sizeof(item->label), "%s", per_title[i].label);
            item->bytes = per_title[i].bytes;
            count++;
        }
    }
    closedir(root);
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (strcmp(items[j].label, items[i].label) > 0) continue;
            BackupCatalogItem tmp = items[i];
            items[i] = items[j];
            items[j] = tmp;
        }
    }
    return count;
}

int backup_restore(const TitleEntry *title, const char *container_path, char *err, size_t err_len, ProgressFn progress) {
    char id[17];
    format_title_id(id, title->title_id);
    char dir[384];
    snprintf(dir, sizeof(dir), TWODSYNC_ROOT "/tmp/restore-%s", id);
    mkdir_p(dir);
    ContainerInfo info;
    if (container_unpack_dir(container_path, dir, &info, err, (unsigned)err_len) != 0) return -1;
    if (info.title_id != title->title_id) {
        snprintf(err, err_len, "This backup belongs to a different title");
        return -1;
    }
    SaveSnapshot snap;
    memset(&snap, 0, sizeof(snap));
    snap.has_secure = info.has_secure_value;
    snap.secure_value = info.secure_value;
    snap.file_count = (int)info.file_count;
    if (progress) progress("Writing save archive", 60);
    return save_import_from(title, dir, &snap, err, err_len, progress);
}
