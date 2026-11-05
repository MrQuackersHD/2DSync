#include "container.h"
#include "sha256.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void set_err(char *err, unsigned err_len, const char *msg) {
    if (!err || err_len == 0) return;
    snprintf(err, err_len, "%s", msg);
}

static int path_ok(const char *rel) {
    if (!rel || !rel[0] || rel[0] == '/' || rel[0] == '\\') return 0;
    if (strlen(rel) > 240) return 0;
    if (strstr(rel, "..")) return 0;
    for (const char *p = rel; *p; p++) {
        if (*p == '\\' || *p == ':' || (unsigned char)*p < 0x20) return 0;
    }
    return 1;
}

static void wr_u16(FILE *f, uint16_t v) {
    uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)};
    fwrite(b, 1, 2, f);
}

static void wr_u32(FILE *f, uint32_t v) {
    uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    fwrite(b, 1, 4, f);
}

static void wr_u64(FILE *f, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; i++) b[i] = (uint8_t)(v >> (8 * i));
    fwrite(b, 1, 8, f);
}

static int rd_full(FILE *f, void *dst, size_t n) {
    return fread(dst, 1, n, f) == n;
}

static int rd_u16(FILE *f, uint16_t *v) {
    uint8_t b[2];
    if (!rd_full(f, b, 2)) return 0;
    *v = (uint16_t)(b[0] | (b[1] << 8));
    return 1;
}

static int rd_u32(FILE *f, uint32_t *v) {
    uint8_t b[4];
    if (!rd_full(f, b, 4)) return 0;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int rd_u64(FILE *f, uint64_t *v) {
    uint8_t b[8];
    if (!rd_full(f, b, 8)) return 0;
    uint64_t x = 0;
    for (int i = 0; i < 8; i++) x |= ((uint64_t)b[i]) << (8 * i);
    *v = x;
    return 1;
}

typedef struct FileEnt {
    char rel[256];
    uint8_t kind;
} FileEnt;

static int cmp_ent(const void *a, const void *b) {
    return strcmp(((const FileEnt *)a)->rel, ((const FileEnt *)b)->rel);
}

static int collect(const char *root, const char *subdir, uint8_t kind, FileEnt *out, int *count, uint64_t *total, char *err, unsigned err_len) {
    char base[512];
    snprintf(base, sizeof(base), "%s/%s", root, subdir);
    DIR *dir = opendir(base);
    if (!dir) return 0;

    char stack[64][256];
    int sp = 0;
    stack[sp++][0] = 0;

    while (sp > 0) {
        char rel[256];
        snprintf(rel, sizeof(rel), "%s", stack[--sp]);
        char full[768];
        if (rel[0]) snprintf(full, sizeof(full), "%s/%s", base, rel);
        else snprintf(full, sizeof(full), "%s", base);
        DIR *d = opendir(full);
        if (!d) {
            set_err(err, err_len, "Could not read save directory");
            closedir(dir);
            return -1;
        }
        struct dirent *de;
        while ((de = readdir(d)) != NULL) {
            if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
            char child_rel[256];
            if (rel[0]) snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, de->d_name);
            else snprintf(child_rel, sizeof(child_rel), "%s", de->d_name);
            char child_full[768];
            snprintf(child_full, sizeof(child_full), "%s/%s", base, child_rel);
            struct stat st;
            if (stat(child_full, &st) != 0) continue;
            if (S_ISDIR(st.st_mode)) {
                if (sp >= 64) {
                    set_err(err, err_len, "Save directory is too deep");
                    closedir(d);
                    closedir(dir);
                    return -1;
                }
                snprintf(stack[sp], sizeof(stack[0]), "%s", child_rel);
                sp++;
            } else if (S_ISREG(st.st_mode)) {
                if (*count >= CONTAINER_MAX_FILES) {
                    set_err(err, err_len, "Save has too many files");
                    closedir(d);
                    closedir(dir);
                    return -1;
                }
                if ((uint64_t)st.st_size > CONTAINER_MAX_FILE) {
                    set_err(err, err_len, "A save file is too large");
                    closedir(d);
                    closedir(dir);
                    return -1;
                }
                *total += (uint64_t)st.st_size;
                if (*total > CONTAINER_MAX_TOTAL) {
                    set_err(err, err_len, "Save is larger than 32 MiB");
                    closedir(d);
                    closedir(dir);
                    return -1;
                }
                char stored[300];
                snprintf(stored, sizeof(stored), "%s/%s", subdir, child_rel);
                if (!path_ok(stored)) {
                    set_err(err, err_len, "Save path is not allowed");
                    closedir(d);
                    closedir(dir);
                    return -1;
                }
                snprintf(out[*count].rel, sizeof(out[*count].rel), "%s", stored);
                out[*count].kind = kind;
                (*count)++;
            }
        }
        closedir(d);
    }
    closedir(dir);
    return 0;
}

static int hash_file(Sha256 *ctx, const char *path, uint32_t *size_out) {
    FILE *in = fopen(path, "rb");
    if (!in) return -1;
    uint32_t total = 0;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        sha256_update(ctx, buf, n);
        total += (uint32_t)n;
    }
    int bad = ferror(in);
    fclose(in);
    if (bad) return -1;
    *size_out = total;
    return 0;
}

static int hash_tree(const char *dir, char checksum_hex[65], char *err, unsigned err_len) {
    FileEnt ents[CONTAINER_MAX_FILES];
    int count = 0;
    uint64_t total = 0;
    if (collect(dir, "savedata", 1, ents, &count, &total, err, err_len) < 0) return -1;
    if (collect(dir, "extdata", 2, ents, &count, &total, err, err_len) < 0) return -1;
    if (count == 0) {
        set_err(err, err_len, "No save files found");
        return -1;
    }
    qsort(ents, (size_t)count, sizeof(FileEnt), cmp_ent);
    Sha256 ctx;
    sha256_init(&ctx);
    for (int i = 0; i < count; i++) {
        char full[768];
        snprintf(full, sizeof(full), "%s/%s", dir, ents[i].rel);
        sha256_update(&ctx, ents[i].rel, strlen(ents[i].rel) + 1);
        struct stat st;
        if (stat(full, &st) != 0) {
            set_err(err, err_len, "Could not read a save file");
            return -1;
        }
        uint32_t sz = (uint32_t)st.st_size;
        uint8_t size_le[4] = {(uint8_t)sz, (uint8_t)(sz >> 8), (uint8_t)(sz >> 16), (uint8_t)(sz >> 24)};
        sha256_update(&ctx, size_le, 4);
        uint32_t got = 0;
        if (hash_file(&ctx, full, &got) != 0 || got != sz) {
            set_err(err, err_len, "Could not read a save file");
            return -1;
        }
    }
    uint8_t dig[32];
    sha256_final(&ctx, dig);
    if (checksum_hex) sha256_hex(dig, checksum_hex);
    return 0;
}

int container_content_hash(const char *dir, char checksum_hex[65], char *err, unsigned err_len) {
    return hash_tree(dir, checksum_hex, err, err_len);
}

static int copy_file(FILE *out, const char *path, uint32_t *size_out) {
    FILE *in = fopen(path, "rb");
    if (!in) return -1;
    uint32_t total = 0;
    uint8_t buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
        if (fwrite(buf, 1, n, out) != n) {
            fclose(in);
            return -1;
        }
        total += (uint32_t)n;
    }
    int bad = ferror(in);
    fclose(in);
    if (bad) return -1;
    *size_out = total;
    return 0;
}

int container_pack_dir(const char *dir, const char *out_path, uint64_t title_id, int has_secure,
                       uint64_t secure_value, uint64_t unix_time, char checksum_hex[65], char *err, unsigned err_len) {
    FileEnt ents[CONTAINER_MAX_FILES];
    int count = 0;
    uint64_t total = 0;
    if (collect(dir, "savedata", 1, ents, &count, &total, err, err_len) < 0) return -1;
    int save_count = count;
    if (collect(dir, "extdata", 2, ents, &count, &total, err, err_len) < 0) return -1;
    if (save_count == 0 && count == 0) {
        set_err(err, err_len, "No save files found");
        return -1;
    }
    qsort(ents, (size_t)count, sizeof(FileEnt), cmp_ent);

    char tmp[768];
    snprintf(tmp, sizeof(tmp), "%s.partial", out_path);
    FILE *out = fopen(tmp, "wb");
    if (!out) {
        set_err(err, err_len, "Could not create container");
        return -1;
    }

    char manifest[4096];
    int m = snprintf(manifest, sizeof(manifest),
                     "{\"title_id\":\"%016llx\",\"files\":[",
                     (unsigned long long)title_id);
    for (int i = 0; i < count && m > 0 && m < (int)sizeof(manifest) - 80; i++) {
        m += snprintf(manifest + m, sizeof(manifest) - (size_t)m, "%s\"%s\"", i ? "," : "", ents[i].rel);
    }
    if (m > 0 && m < (int)sizeof(manifest) - 4) {
        snprintf(manifest + m, sizeof(manifest) - (size_t)m, "]}");
    }
    uint32_t manifest_len = (uint32_t)strlen(manifest);

    fwrite(CONTAINER_MAGIC, 1, 4, out);
    wr_u16(out, CONTAINER_VERSION);
    uint16_t flags = 0;
    if (has_secure) flags |= 1;
    if (count > save_count) flags |= 2;
    wr_u16(out, flags);
    wr_u64(out, title_id);
    wr_u64(out, has_secure ? secure_value : 0);
    wr_u64(out, unix_time);
    wr_u32(out, manifest_len);
    fwrite(manifest, 1, manifest_len, out);

    for (int i = 0; i < count; i++) {
        char full[768];
        snprintf(full, sizeof(full), "%s/%s", dir, ents[i].rel);
        uint8_t kind = ents[i].kind;
        fwrite(&kind, 1, 1, out);
        uint16_t plen = (uint16_t)strlen(ents[i].rel);
        wr_u16(out, plen);
        fwrite(ents[i].rel, 1, plen, out);
        long size_pos = ftell(out);
        wr_u32(out, 0);
        uint32_t sz = 0;
        if (copy_file(out, full, &sz) != 0) {
            fclose(out);
            remove(tmp);
            set_err(err, err_len, "Could not read a save file");
            return -1;
        }
        long end = ftell(out);
        fseek(out, size_pos, SEEK_SET);
        wr_u32(out, sz);
        fseek(out, end, SEEK_SET);
    }
    uint8_t end = 0;
    fwrite(&end, 1, 1, out);
    if (fflush(out) != 0) {
        fclose(out);
        remove(tmp);
        set_err(err, err_len, "Could not finish container");
        return -1;
    }
    fclose(out);

    uint8_t dig[32];
    if (sha256_file(tmp, dig) != 0) {
        remove(tmp);
        set_err(err, err_len, "Could not hash container");
        return -1;
    }
    remove(out_path);
    if (rename(tmp, out_path) != 0) {
        remove(tmp);
        set_err(err, err_len, "Could not store container");
        return -1;
    }
    if (checksum_hex) sha256_hex(dig, checksum_hex);
    return 0;
}

static int ensure_parent(const char *path) {
    char buf[768];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0777);
            *p = '/';
        }
    }
    return 0;
}

static int read_header(FILE *f, ContainerInfo *info, uint32_t *manifest_len, char *err, unsigned err_len) {
    char magic[4];
    if (!rd_full(f, magic, 4) || memcmp(magic, CONTAINER_MAGIC, 4) != 0) {
        set_err(err, err_len, "Not a 2DSync save container");
        return -1;
    }
    uint16_t version = 0, flags = 0;
    if (!rd_u16(f, &version) || version != CONTAINER_VERSION) {
        set_err(err, err_len, "Unsupported save container version");
        return -1;
    }
    if (!rd_u16(f, &flags) || !rd_u64(f, &info->title_id) || !rd_u64(f, &info->secure_value) ||
        !rd_u64(f, &info->unix_time) || !rd_u32(f, manifest_len)) {
        set_err(err, err_len, "Container header is truncated");
        return -1;
    }
    if (*manifest_len > 65536) {
        set_err(err, err_len, "Container manifest is too large");
        return -1;
    }
    info->has_secure_value = (flags & 1) ? 1 : 0;
    info->has_extdata = (flags & 2) ? 1 : 0;
    info->file_count = 0;
    info->total_bytes = 0;
    return 0;
}

int container_read_info(const char *in_path, ContainerInfo *info, char *err, unsigned err_len) {
    FILE *f = fopen(in_path, "rb");
    if (!f) {
        set_err(err, err_len, "Could not open container");
        return -1;
    }
    uint32_t manifest_len = 0;
    int rc = read_header(f, info, &manifest_len, err, err_len);
    fclose(f);
    return rc;
}

int container_unpack_dir(const char *in_path, const char *dir, ContainerInfo *info, char *err, unsigned err_len) {
    FILE *f = fopen(in_path, "rb");
    if (!f) {
        set_err(err, err_len, "Could not open container");
        return -1;
    }
    ContainerInfo local;
    if (!info) info = &local;
    memset(info, 0, sizeof(*info));
    uint32_t manifest_len = 0;
    if (read_header(f, info, &manifest_len, err, err_len) != 0) {
        fclose(f);
        return -1;
    }
    if (fseek(f, manifest_len, SEEK_CUR) != 0) {
        fclose(f);
        set_err(err, err_len, "Container manifest is truncated");
        return -1;
    }

    char savedata[512], extdata[512];
    snprintf(savedata, sizeof(savedata), "%s/savedata", dir);
    snprintf(extdata, sizeof(extdata), "%s/extdata", dir);
    mkdir(dir, 0777);
    mkdir(savedata, 0777);
    mkdir(extdata, 0777);

    uint64_t total = 0;
    for (;;) {
        uint8_t kind = 0;
        if (!rd_full(f, &kind, 1)) {
            fclose(f);
            set_err(err, err_len, "Container ended early");
            return -1;
        }
        if (kind == 0) break;
        if (kind != 1 && kind != 2) {
            fclose(f);
            set_err(err, err_len, "Unknown file kind in container");
            return -1;
        }
        uint16_t plen = 0;
        if (!rd_u16(f, &plen) || plen == 0 || plen > 240) {
            fclose(f);
            set_err(err, err_len, "Bad path in container");
            return -1;
        }
        char rel[241];
        if (!rd_full(f, rel, plen)) {
            fclose(f);
            set_err(err, err_len, "Truncated path in container");
            return -1;
        }
        rel[plen] = 0;
        if (!path_ok(rel) || (kind == 1 && strncmp(rel, "savedata/", 9) != 0) ||
            (kind == 2 && strncmp(rel, "extdata/", 8) != 0)) {
            fclose(f);
            set_err(err, err_len, "Refusing unsafe path in container");
            return -1;
        }
        uint32_t sz = 0;
        if (!rd_u32(f, &sz) || sz > CONTAINER_MAX_FILE) {
            fclose(f);
            set_err(err, err_len, "Save file is too large");
            return -1;
        }
        total += sz;
        if (total > CONTAINER_MAX_TOTAL || info->file_count >= CONTAINER_MAX_FILES) {
            fclose(f);
            set_err(err, err_len, "Container exceeds size limits");
            return -1;
        }
        char full[768];
        snprintf(full, sizeof(full), "%s/%s", dir, rel);
        ensure_parent(full);
        FILE *out = fopen(full, "wb");
        if (!out) {
            fclose(f);
            set_err(err, err_len, "Could not write restored file");
            return -1;
        }
        uint32_t left = sz;
        uint8_t buf[4096];
        while (left) {
            uint32_t chunk = left > sizeof(buf) ? (uint32_t)sizeof(buf) : left;
            if (!rd_full(f, buf, chunk) || fwrite(buf, 1, chunk, out) != chunk) {
                fclose(out);
                fclose(f);
                set_err(err, err_len, "Container data is truncated");
                return -1;
            }
            left -= chunk;
        }
        fclose(out);
        info->file_count++;
        info->total_bytes = (uint32_t)total;
    }
    fclose(f);
    if (info->file_count == 0) {
        set_err(err, err_len, "Container has no save files");
        return -1;
    }
    return 0;
}
