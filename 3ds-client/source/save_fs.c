#include "save_fs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void set_err(char *err, size_t err_len, const char *msg) {
    if (err && err_len) snprintf(err, err_len, "%s", msg);
}

static Result open_user_save(FS_Archive *archive, const TitleEntry *title) {
    u32 words[3] = {(u32)title->media, (u32)title->title_id, (u32)(title->title_id >> 32)};
    FS_Path path = {PATH_BINARY, sizeof(words), words};
    return FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, path);
}

static Result open_extdata(FS_Archive *archive, u64 ext_id) {
    FS_ExtSaveDataInfo info;
    memset(&info, 0, sizeof(info));
    info.mediaType = MEDIATYPE_SD;
    info.saveId = ext_id;
    FS_Path path = {PATH_BINARY, sizeof(info), &info};
    return FSUSER_OpenArchive(archive, ARCHIVE_EXTDATA, path);
}

static int ascii_path(FS_Path *path, const char *text) {
    *path = fsMakePath(PATH_ASCII, text);
    return 1;
}

static int copy_out_file(FS_Archive archive, const char *archive_path, const char *dest) {
    FS_Path path;
    ascii_path(&path, archive_path);
    Handle file = 0;
    if (R_FAILED(FSUSER_OpenFile(&file, archive, path, FS_OPEN_READ, 0))) return -1;
    u64 size = 0;
    FSFILE_GetSize(file, &size);
    if (size > 8ull * 1024ull * 1024ull) {
        FSFILE_Close(file);
        return -2;
    }
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.partial", dest);
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        FSFILE_Close(file);
        return -1;
    }
    static u8 buf[32 * 1024];
    u64 off = 0;
    while (off < size) {
        u32 chunk = (u32)((size - off) > sizeof(buf) ? sizeof(buf) : (size - off));
        u32 read = 0;
        if (R_FAILED(FSFILE_Read(file, &read, off, buf, chunk)) || read != chunk) {
            fclose(out);
            remove(tmp);
            FSFILE_Close(file);
            return -1;
        }
        if (fwrite(buf, 1, read, out) != read) {
            fclose(out);
            remove(tmp);
            FSFILE_Close(file);
            return -1;
        }
        off += read;
    }
    fflush(out);
    fclose(out);
    FSFILE_Close(file);
    /* A second backup or upload copies onto files the first one already
       wrote. The 3DS refuses to rename onto an existing file. */
    remove(dest);
    if (rename(tmp, dest) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}

static int export_archive(FS_Archive archive, const char *root, int kind, int *count, char *err, size_t err_len, ProgressFn progress) {
    typedef struct { char path[256]; } Item;
    Item *queue = calloc(128, sizeof(Item));
    if (!queue) {
        set_err(err, err_len, "Out of memory");
        return -1;
    }
    int qh = 0, qt = 0;
    snprintf(queue[qt++].path, 256, "/");
    while (qh < qt) {
        char current[256];
        snprintf(current, sizeof(current), "%s", queue[qh++].path);
        FS_Path path;
        ascii_path(&path, current);
        Handle dir = 0;
        if (R_FAILED(FSUSER_OpenDirectory(&dir, archive, path))) {
            free(queue);
            set_err(err, err_len, "Could not read the save archive");
            return -1;
        }
        FS_DirectoryEntry ent;
        u32 read = 0;
        do {
            read = 0;
            if (R_FAILED(FSDIR_Read(dir, &read, 1, &ent)) || !read) break;
            char name[256];
            save_utf16_to_utf8(name, sizeof(name), ent.name);
            if (!name[0] || strcmp(name, ".") == 0 || strcmp(name, "..") == 0) continue;
            if (strstr(name, "..") || strchr(name, '/') || strchr(name, '\\')) continue;
            char child[256];
            if (strcmp(current, "/") == 0) snprintf(child, sizeof(child), "/%s", name);
            else snprintf(child, sizeof(child), "%s/%s", current, name);
            if (ent.attributes & FS_ATTRIBUTE_DIRECTORY) {
                if (qt >= 128) {
                    FSDIR_Close(dir);
                    free(queue);
                    set_err(err, err_len, "Save directory is too deep");
                    return -1;
                }
                snprintf(queue[qt++].path, 256, "%s", child);
            } else {
                const char *rel = child[0] == '/' ? child + 1 : child;
                char folder[16];
                snprintf(folder, sizeof(folder), "%s", kind == 2 ? "extdata" : "savedata");
                char dest[512];
                snprintf(dest, sizeof(dest), "%s/%s/%s", root, folder, rel);
                char *slash = strrchr(dest, '/');
                if (slash) {
                    *slash = 0;
                    mkdir_p(dest);
                    *slash = '/';
                }
                int copied = copy_out_file(archive, child, dest);
                if (copied != 0) {
                    FSDIR_Close(dir);
                    free(queue);
                    set_err(err, err_len, copied == -2 ? "A save file is too large" : "Could not copy a save file");
                    return -1;
                }
                (*count)++;
                if (progress) progress(kind == 2 ? "Copying extra data" : "Copying save", -1);
            }
        } while (read);
        FSDIR_Close(dir);
    }
    free(queue);
    return 0;
}

static void read_secure(const TitleEntry *title, SaveSnapshot *snap) {
    snap->has_secure = 0;
    snap->secure_value = 0;
    if (title->media != MEDIATYPE_SD) return;
    u32 low = (u32)title->title_id;
    u32 unique = (low >> 8) & 0xFFFFFF;
    u8 variation = (u8)(low & 0xFF);
    bool exists = false;
    u64 value = 0;
    if (R_SUCCEEDED(FSUSER_GetSaveDataSecureValue(&exists, &value, SECUREVALUE_SLOT_SD, unique, variation)) && exists) {
        snap->has_secure = 1;
        snap->secure_value = value;
    }
}

static void write_secure(const TitleEntry *title, const SaveSnapshot *snap) {
    if (!snap || !snap->has_secure || title->media != MEDIATYPE_SD) return;
    u32 low = (u32)title->title_id;
    u32 unique = (low >> 8) & 0xFFFFFF;
    u8 variation = (u8)(low & 0xFF);
    FSUSER_SetSaveDataSecureValue(snap->secure_value, SECUREVALUE_SLOT_SD, unique, variation);
}

int save_export_to(const TitleEntry *title, const char *dir, SaveSnapshot *snap, char *err, size_t err_len, ProgressFn progress) {
    memset(snap, 0, sizeof(*snap));
    if (!title_class_supported(title->kind)) {
        set_err(err, err_len, title_class_reason(title->kind));
        return -1;
    }
    mkdir_p(dir);
    char save_dir[512], ext_dir[512];
    snprintf(save_dir, sizeof(save_dir), "%s/savedata", dir);
    snprintf(ext_dir, sizeof(ext_dir), "%s/extdata", dir);
    mkdir_p(save_dir);
    mkdir_p(ext_dir);
    FS_Archive archive = 0;
    Result rc = open_user_save(&archive, title);
    if (R_FAILED(rc)) {
        snprintf(err, err_len, "Could not open the save (0x%08lX). Use the CIA on CFW.", (unsigned long)rc);
        return -1;
    }
    int count = 0;
    int result = export_archive(archive, dir, 1, &count, err, err_len, progress);
    FSUSER_CloseArchive(archive);
    if (result != 0) return result;
    if (title->has_extdata && title->extdata_id) {
        FS_Archive ext = 0;
        if (R_SUCCEEDED(open_extdata(&ext, title->extdata_id))) {
            result = export_archive(ext, dir, 2, &count, err, err_len, progress);
            FSUSER_CloseArchive(ext);
            if (result != 0) return result;
        }
    }
    if (count == 0) {
        set_err(err, err_len, "This title has no save files");
        return -1;
    }
    snap->file_count = count;
    read_secure(title, snap);
    return 0;
}

static int wipe_archive(FS_Archive archive, char *err, size_t err_len) {
    FS_Path root;
    ascii_path(&root, "/");
    Handle dir = 0;
    if (R_FAILED(FSUSER_OpenDirectory(&dir, archive, root))) {
        set_err(err, err_len, "Could not open save for restore");
        return -1;
    }
    char names[64][256];
    int dirs[64];
    int count = 0;
    FS_DirectoryEntry ent;
    u32 read = 0;
    do {
        read = 0;
        if (R_FAILED(FSDIR_Read(dir, &read, 1, &ent)) || !read) break;
        if (count >= 64) {
            FSDIR_Close(dir);
            set_err(err, err_len, "Save has too many root entries to replace safely");
            return -1;
        }
        save_utf16_to_utf8(names[count], sizeof(names[count]), ent.name);
        dirs[count] = (ent.attributes & FS_ATTRIBUTE_DIRECTORY) ? 1 : 0;
        count++;
    } while (read);
    FSDIR_Close(dir);
    for (int i = 0; i < count; i++) {
        char path_text[280];
        snprintf(path_text, sizeof(path_text), "/%s", names[i]);
        FS_Path path;
        ascii_path(&path, path_text);
        Result rc = dirs[i] ? FSUSER_DeleteDirectoryRecursively(archive, path) : FSUSER_DeleteFile(archive, path);
        if (R_FAILED(rc)) {
            snprintf(err, err_len, "Could not clear the old save (0x%08lX)", (unsigned long)rc);
            return -1;
        }
    }
    return 0;
}

static int import_tree(FS_Archive archive, const char *disk_root, char *err, size_t err_len) {
    char stack[64][256];
    int sp = 0;
    stack[0][0] = 0;
    sp = 1;
    while (sp > 0) {
        char rel[256];
        snprintf(rel, sizeof(rel), "%s", stack[--sp]);
        char full[512];
        if (rel[0]) snprintf(full, sizeof(full), "%s/%s", disk_root, rel);
        else snprintf(full, sizeof(full), "%s", disk_root);
        DIR *dir = opendir(full);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            char child_rel[256];
            if (rel[0]) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
            else snprintf(child_rel, sizeof(child_rel), "%s", de->d_name);
            if (strstr(child_rel, "..")) continue;
            char child_full[512];
            snprintf(child_full, sizeof(child_full), "%s/%s", disk_root, child_rel);
            struct stat st;
            if (stat(child_full, &st) != 0) continue;
            char archive_path[280];
            snprintf(archive_path, sizeof(archive_path), "/%s", child_rel);
            FS_Path path;
            ascii_path(&path, archive_path);
            if (S_ISDIR(st.st_mode)) {
                FSUSER_CreateDirectory(archive, path, 0);
                if (sp >= 64) {
                    closedir(dir);
                    set_err(err, err_len, "Restore directory is too deep");
                    return -1;
                }
                snprintf(stack[sp++], 256, "%s", child_rel);
            } else if (S_ISREG(st.st_mode)) {
                if ((u64)st.st_size > 8ull * 1024ull * 1024ull) {
                    closedir(dir);
                    set_err(err, err_len, "A restored file is too large");
                    return -1;
                }
                FSUSER_DeleteFile(archive, path);
                if (R_FAILED(FSUSER_CreateFile(archive, path, 0, (u64)st.st_size))) {
                    closedir(dir);
                    set_err(err, err_len, "Could not create a save file");
                    return -1;
                }
                Handle file = 0;
                if (R_FAILED(FSUSER_OpenFile(&file, archive, path, FS_OPEN_WRITE, 0))) {
                    closedir(dir);
                    set_err(err, err_len, "Could not open a save file for writing");
                    return -1;
                }
                FILE *in = fopen(child_full, "rb");
                if (!in) {
                    FSFILE_Close(file);
                    closedir(dir);
                    set_err(err, err_len, "Could not read the backup");
                    return -1;
                }
                static u8 buf[32 * 1024];
                u64 off = 0;
                size_t n;
                while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
                    u32 written = 0;
                    if (R_FAILED(FSFILE_Write(file, &written, off, buf, (u32)n, FS_WRITE_FLUSH | FS_WRITE_UPDATE_TIME)) || written != n) {
                        fclose(in);
                        FSFILE_Close(file);
                        closedir(dir);
                        set_err(err, err_len, "Could not write the save");
                        return -1;
                    }
                    off += written;
                }
                fclose(in);
                FSFILE_Close(file);
            }
        }
        closedir(dir);
    }
    return 0;
}

static int import_one(const TitleEntry *title, int extdata, const char *dir, char *err, size_t err_len, ProgressFn progress) {
    char root[512];
    snprintf(root, sizeof(root), "%s/%s", dir, extdata ? "extdata" : "savedata");
    struct stat st;
    if (stat(root, &st) != 0) return 0;
    FS_Archive archive = 0;
    Result rc = extdata ? open_extdata(&archive, title->extdata_id) : open_user_save(&archive, title);
    if (R_FAILED(rc)) {
        if (extdata) return 0;
        snprintf(err, err_len, "Could not open the save (0x%08lX)", (unsigned long)rc);
        return -1;
    }
    if (progress) progress(extdata ? "Restoring extra data" : "Restoring save", 40);
    if (wipe_archive(archive, err, err_len) != 0) {
        FSUSER_CloseArchive(archive);
        return -1;
    }
    if (import_tree(archive, root, err, err_len) != 0) {
        FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
        FSUSER_CloseArchive(archive);
        return -1;
    }
    rc = FSUSER_ControlArchive(archive, ARCHIVE_ACTION_COMMIT_SAVE_DATA, NULL, 0, NULL, 0);
    FSUSER_CloseArchive(archive);
    if (R_FAILED(rc)) {
        snprintf(err, err_len, "Could not commit the save (0x%08lX)", (unsigned long)rc);
        return -1;
    }
    return 0;
}

int save_import_from(const TitleEntry *title, const char *dir, const SaveSnapshot *snap, char *err, size_t err_len, ProgressFn progress) {
    if (!title_class_supported(title->kind)) {
        set_err(err, err_len, title_class_reason(title->kind));
        return -1;
    }
    if (import_one(title, 0, dir, err, err_len, progress) != 0) return -1;
    if (title->extdata_id && import_one(title, 1, dir, err, err_len, progress) != 0) return -1;
    write_secure(title, snap);
    if (progress) progress("Save committed", 100);
    return 0;
}
