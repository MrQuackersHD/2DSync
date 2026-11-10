#include "titles.h"
#include "icon_tiles.h"
#include "util.h"

#include <strings.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>

#define OWN_TITLE 0x0004000002D59C00ULL

static Result open_user_save(FS_Archive *archive, FS_MediaType media, u64 title_id) {
    u32 words[3];
    words[0] = (u32)media;
    words[1] = (u32)title_id;
    words[2] = (u32)(title_id >> 32);
    FS_Path path;
    path.type = PATH_BINARY;
    path.size = sizeof(words);
    path.data = words;
    return FSUSER_OpenArchive(archive, ARCHIVE_USER_SAVEDATA, path);
}

static Result open_extdata(FS_Archive *archive, u64 ext_id) {
    FS_ExtSaveDataInfo info;
    memset(&info, 0, sizeof(info));
    info.mediaType = MEDIATYPE_SD;
    info.saveId = ext_id;
    FS_Path path;
    path.type = PATH_BINARY;
    path.size = sizeof(info);
    path.data = &info;
    return FSUSER_OpenArchive(archive, ARCHIVE_EXTDATA, path);
}

static Result open_icon_at(Handle *out, u64 title_id, FS_MediaType media, u32 ncch_part, int media_offset) {
    u8 arch[0x10];
    u8 file[0x14];
    memset(arch, 0, sizeof(arch));
    memset(file, 0, sizeof(file));
    memcpy(arch, &title_id, 8);
    arch[media_offset] = (u8)media;
    memcpy(file + 8, &ncch_part, 4);
    memcpy(file + 12, "icon", 4);
    FS_Path ap = {PATH_BINARY, sizeof(arch), arch};
    FS_Path fp = {PATH_BINARY, sizeof(file), file};
    return FSUSER_OpenFileDirectly(out, ARCHIVE_SAVEDATA_AND_CONTENT, ap, fp, FS_OPEN_READ, 0);
}

static Result open_icon(Handle *out, u64 title_id, FS_MediaType media, int media_offset) {
    return open_icon_at(out, title_id, media, 2, media_offset);
}

static int product_is_ds(const char *product) {
    if (!product || !product[0]) return 0;
    return strncasecmp(product, "TWL-", 4) == 0 || strncasecmp(product, "NTR-", 4) == 0;
}

static int title_is_twl(u64 title_id, FS_MediaType media, const char *product) {
    uint32_t high = (uint32_t)(title_id >> 32);
    if (high == 0x00030004 || high == 0x00030005 || high == 0x0003000F) return 1;
    if (high >= 0x00048000 && high <= 0x0004801F) return 1;
    if (media == MEDIATYPE_GAME_CARD && product_is_ds(product)) return 1;
    return 0;
}

static void polish_title_name(TitleEntry *title);

static void name_from_legacy_banner(char *out, size_t out_len, const u8 *bnr, u8 language) {
    const u16 *titles = (const u16 *)(bnr + 0x20);
    int langs[3] = {language, CFG_LANGUAGE_EN, CFG_LANGUAGE_JP};
    for (int i = 0; i < 3; i++) {
        int lang = langs[i];
        if (lang < 0 || lang > 5) continue;
        const u16 *desc = titles + (size_t)lang * 0x80;
        if (desc[0]) {
            save_utf16_to_utf8(out, out_len, desc);
            return;
        }
    }
    snprintf(out, out_len, "Unknown title");
}

static void read_legacy_meta(TitleEntry *title, u8 language) {
    u8 bnr[0x2400];
    if (R_FAILED(FSUSER_GetLegacyBannerData(title->media, title->title_id, bnr))) return;
    name_from_legacy_banner(title->name, sizeof(title->name), bnr, language);
    polish_title_name(title);
}

static Result open_icon_any(Handle *out, u64 title_id, FS_MediaType media) {
    uint32_t high = (uint32_t)(title_id >> 32);
    int twl = high == 0x00030004 || high == 0x00030005 || high == 0x0003000F || (high >= 0x00048000 && high <= 0x0004801F);
    static const u32 parts[] = {0, 1, 2};
    size_t start = 0;
    if (twl) {
        for (int media_offset = 8; media_offset <= 9; media_offset++) {
            Result rc = open_icon_at(out, title_id, media, 0, media_offset);
            if (R_SUCCEEDED(rc)) return rc;
        }
        start = 1;
    }
    for (size_t pi = start; pi < sizeof(parts) / sizeof(parts[0]); pi++) {
        for (int media_offset = 8; media_offset <= 9; media_offset++) {
            Result rc = open_icon_at(out, title_id, media, parts[pi], media_offset);
            if (R_SUCCEEDED(rc)) return rc;
        }
    }
    return -1;
}

static void name_from_smdh(char *out, size_t out_len, const u8 *smdh, u8 language) {
    if (memcmp(smdh, "SMDH", 4) != 0) {
        snprintf(out, out_len, "Unknown title");
        return;
    }
    int langs[3] = {language, CFG_LANGUAGE_EN, CFG_LANGUAGE_JP};
    for (int i = 0; i < 3; i++) {
        int lang = langs[i];
        if (lang < 0 || lang > 11) continue;
        const u16 *desc = (const u16 *)(smdh + 0x8 + (size_t)lang * 0x200);
        if (desc[0]) {
            save_utf16_to_utf8(out, out_len, desc);
            return;
        }
    }
    snprintf(out, out_len, "Unknown title");
}

static void install_icon(TitleEntry *title, const u8 *smdh);

static void install_placeholder_icon(TitleEntry *title, u16 inner, u16 outer) {
    if (title->icon_ready) return;
    if (!C3D_TexInit(&title->tex, 64, 64, GPU_RGB565)) return;
    uint16_t *px = (uint16_t *)title->tex.data;
    for (int ty = 0; ty < 8; ty++) {
        for (int tx = 0; tx < 8; tx++) {
            uint16_t *tile = px + (ty * 8 + tx) * 64;
            int edge = (tx == 0 || ty == 0 || tx == 7 || ty == 7);
            for (int i = 0; i < 64; i++) tile[i] = edge ? outer : inner;
        }
    }
    C3D_TexFlush(&title->tex);
    C3D_TexSetFilter(&title->tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&title->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    title->sub.width = 48;
    title->sub.height = 48;
    title->sub.left = 0.0f;
    title->sub.right = 48.0f / 64.0f;
    title->sub.top = 1.0f;
    title->sub.bottom = 1.0f - (48.0f / 64.0f);
    title->icon_ready = 1;
}

static void polish_title_name(TitleEntry *title) {
    if (title->name[0] && strcmp(title->name, "Unknown title") != 0 && strncmp(title->name, "Title ", 6) != 0) return;
    if (!title->product[0]) return;
    if (title->kind == TITLE_SUPPORTED_DSIWARE) {
        snprintf(title->name, sizeof(title->name), "DSiWare %s", title->product);
    } else if (title->kind == TITLE_SUPPORTED_DS_CARD) {
        if (title->product[0]) snprintf(title->name, sizeof(title->name), "%s", title->product);
    } else if (title->kind == TITLE_SUPPORTED_DS_SAVE) {
        return;
    } else {
        snprintf(title->name, sizeof(title->name), "Game %s", title->product);
    }
}

static void read_meta(TitleEntry *title, u8 language) {
    Handle file = 0;
    Result rc = open_icon_any(&file, title->title_id, title->media);
    if (R_FAILED(rc)) {
        if (title_is_twl(title->title_id, title->media, title->product)) read_legacy_meta(title, language);
        polish_title_name(title);
        if (!title->name[0] || strcmp(title->name, "Unknown title") == 0)
            snprintf(title->name, sizeof(title->name), "Title %016llX", (unsigned long long)title->title_id);
        if (title->kind == TITLE_SUPPORTED_DSIWARE || title->kind == TITLE_SUPPORTED_DS_SAVE || title->kind == TITLE_SUPPORTED_DS_CARD ||
            title_is_twl(title->title_id, title->media, title->product)) {
            if (title->kind == TITLE_SUPPORTED_DS_CARD || product_is_ds(title->product))
                install_placeholder_icon(title, 0x528A, 0x2A44);
            else
                install_placeholder_icon(title, 0x34A4, 0x2244);
        }
        return;
    }
    u8 *smdh = malloc(0x36C0);
    if (!smdh) {
        FSFILE_Close(file);
        snprintf(title->name, sizeof(title->name), "Title %016llX", (unsigned long long)title->title_id);
        return;
    }
    u32 read = 0;
    rc = FSFILE_Read(file, &read, 0, smdh, 0x36C0);
    FSFILE_Close(file);
    if (R_FAILED(rc) || read < 0x24C0) {
        free(smdh);
        snprintf(title->name, sizeof(title->name), "Title %016llX", (unsigned long long)title->title_id);
        return;
    }
    name_from_smdh(title->name, sizeof(title->name), smdh, language);
    polish_title_name(title);
    if (read >= 0x36C0) install_icon(title, smdh);
    else if (title->kind == TITLE_SUPPORTED_DSIWARE) install_placeholder_icon(title, 0x34A4, 0x2244);
    else if (title->kind == TITLE_SUPPORTED_DS_CARD) install_placeholder_icon(title, 0x528A, 0x2A44);
    free(smdh);
}

static void install_icon(TitleEntry *title, const u8 *smdh) {
    if (title->icon_ready) return;
    if (!C3D_TexInit(&title->tex, 64, 64, GPU_RGB565)) return;
    icon_tiles_to_tex64((uint16_t *)title->tex.data, (const uint16_t *)(smdh + 0x24C0));
    C3D_TexFlush(&title->tex);
    C3D_TexSetFilter(&title->tex, GPU_NEAREST, GPU_NEAREST);
    C3D_TexSetWrap(&title->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    title->sub.width = 48;
    title->sub.height = 48;
    title->sub.left = 0.0f;
    title->sub.right = 48.0f / 64.0f;
    title->sub.top = 1.0f;
    title->sub.bottom = 1.0f - (48.0f / 64.0f);
    title->icon_ready = 1;
}

void titles_load_icon(TitleEntry *title) {
    if (title->icon_ready) return;
    Handle file = 0;
    Result rc = open_icon_any(&file, title->title_id, title->media);
    if (R_FAILED(rc)) {
        if (title->kind == TITLE_SUPPORTED_DSIWARE || title->kind == TITLE_SUPPORTED_DS_SAVE || title->kind == TITLE_SUPPORTED_DS_CARD) {
            install_placeholder_icon(title, 0x34A4, 0x2244);
        }
        return;
    }
    u8 *smdh = malloc(0x36C0);
    if (!smdh) {
        FSFILE_Close(file);
        return;
    }
    u32 read = 0;
    rc = FSFILE_Read(file, &read, 0, smdh, 0x36C0);
    FSFILE_Close(file);
    if (R_FAILED(rc) || read < 0x36C0) {
        free(smdh);
        return;
    }
    install_icon(title, smdh);
    free(smdh);
}

static void probe_save(TitleEntry *title) {
    if (title->kind == TITLE_SUPPORTED_DS_SAVE) {
        struct stat st;
        if (title->ds_save_path[0] && stat(title->ds_save_path, &st) == 0 && st.st_size > 0) {
            title->has_save = 1;
            snprintf(title->note, sizeof(title->note), "DS save on SD card");
        } else {
            title->has_save = 0;
            snprintf(title->note, sizeof(title->note), "DS save file missing");
        }
        return;
    }
    if (!title_class_supported(title->kind)) {
        snprintf(title->note, sizeof(title->note), "%s", title_class_reason(title->kind));
        return;
    }
    if (title->ds_save_path[0]) {
        struct stat st;
        if (stat(title->ds_save_path, &st) == 0 && st.st_size > 0) {
            title->has_save = 1;
            snprintf(title->note, sizeof(title->note), "DS save on SD card");
            return;
        }
    }
    FS_Archive archive = 0;
    Result rc = open_user_save(&archive, title->media, title->title_id);
    if (R_SUCCEEDED(rc)) {
        title->has_save = 1;
        FSUSER_CloseArchive(archive);
        snprintf(title->note, sizeof(title->note), "Save archive found");
    } else if (title->kind == TITLE_SUPPORTED_DS_CARD || title->media == MEDIATYPE_GAME_CARD) {
        snprintf(title->note, sizeof(title->note), title->ds_save_path[0] ? "DS save on SD card" : "Insert card or add SD save");
        title->has_save = title->ds_save_path[0] != 0;
    } else {
        snprintf(title->note, sizeof(title->note), "No accessible save (0x%08lX)", (unsigned long)rc);
        title->has_save = 0;
    }
    u64 ext = 0;
    if (R_SUCCEEDED(AM_GetTitleExtDataId(&ext, title->media, title->title_id)) && ext) {
        title->extdata_id = ext;
        FS_Archive ext_arch = 0;
        if (R_SUCCEEDED(open_extdata(&ext_arch, ext))) {
            title->has_extdata = 1;
            FSUSER_CloseArchive(ext_arch);
        }
    }
}

void titles_free_icons(TitleList *list) {
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].icon_ready) {
            C3D_TexDelete(&list->items[i].tex);
            list->items[i].icon_ready = 0;
        }
    }
}

static int add_media(TitleList *list, FS_MediaType media, u8 language, int include_unsupported, void (*progress)(const char *, int)) {
    u32 count = 0;
    Result rc = AM_GetTitleCount(media, &count);
    if (R_FAILED(rc) || count == 0) return 0;
    if (count > 400) count = 400;
    u64 *ids = malloc(sizeof(u64) * count);
    AM_TitleInfo *infos = malloc(sizeof(AM_TitleInfo) * count);
    if (!ids || !infos) {
        free(ids);
        free(infos);
        return -1;
    }
    u32 read = 0;
    rc = AM_GetTitleList(&read, media, count, ids);
    if (R_FAILED(rc)) {
        free(ids);
        free(infos);
        return -1;
    }
    rc = AM_GetTitleInfo(media, read, ids, infos);
    if (R_FAILED(rc)) {
        free(ids);
        free(infos);
        return -1;
    }
    for (u32 i = 0; i < read && list->count < MAX_TITLES; i++) {
        if (ids[i] == OWN_TITLE) continue;
        TitleEntry *title = &list->items[list->count];
        memset(title, 0, sizeof(*title));
        title->title_id = ids[i];
        title->media = media;
        title->version = infos[i].version;
        title->kind = classify_title(ids[i], (int)media);
        if (!include_unsupported && !title_class_supported(title->kind)) continue;
        AM_GetTitleProductCode(media, ids[i], title->product);
        title->product[16] = 0;
        if (media == MEDIATYPE_GAME_CARD && title->kind == TITLE_SUPPORTED_GAME && product_is_ds(title->product))
            title->kind = TITLE_SUPPORTED_DS_CARD;
        read_meta(title, language);
        probe_save(title);
        if (title_class_supported(title->kind)) list->supported_count++;
        if (!title->has_save && title_class_supported(title->kind) && strstr(title->note, "0x")) list->save_access_failures++;
        list->count++;
        if (progress) {
            char msg[64];
            snprintf(msg, sizeof(msg), "Scanning titles %d", list->count);
            progress(msg, -1);
        }
    }
    free(ids);
    free(infos);
    return 0;
}

static void scan_ds_saves(TitleList *list, void (*progress)(const char *, int));

int titles_scan(TitleList *list, int include_unsupported, void (*progress)(const char *, int)) {
    titles_free_icons(list);
    memset(list, 0, sizeof(*list));
    u8 language = CFG_LANGUAGE_EN;
    CFGU_GetSystemLanguage(&language);
    u32 cart_titles = 0;
    AM_GetTitleCount(MEDIATYPE_GAME_CARD, &cart_titles);
    bool inserted = false;
    FSUSER_CardSlotIsInserted(&inserted);
    if (inserted || cart_titles > 0) {
        if (progress) progress("Scanning cartridge", 0);
        if (add_media(list, MEDIATYPE_GAME_CARD, language, include_unsupported, progress) != 0) return -1;
    }
    if (progress) progress("Scanning SD titles", 15);
    if (add_media(list, MEDIATYPE_SD, language, include_unsupported, progress) != 0) return -1;
    if (progress) progress("Scanning DSiWare", 50);
    if (add_media(list, MEDIATYPE_NAND, language, include_unsupported, progress) != 0) return -1;
    if (progress) progress("Scanning DS saves", 65);
    scan_ds_saves(list, progress);
    if (progress) progress("Scan complete", 100);
    titles_cache_names(list);
    return 0;
}

const char *media_label(FS_MediaType media) {
    if (media == MEDIATYPE_GAME_CARD) return "Cartridge";
    if (media == MEDIATYPE_SD) return "Digital";
    if (media == MEDIATYPE_NAND) return "NAND";
    return "Unknown";
}

int title_entry_visible(const TitleEntry *entry, int show_unsupported) {
    if (!entry) return 0;
    return show_unsupported || title_class_supported(entry->kind);
}

int title_entry_matches(const TitleEntry *entry, const char *search) {
    if (!entry) return 0;
    if (!search || !search[0]) return 1;
    char id[17];
    format_title_id(id, entry->title_id);
    if (str_contains_ci(entry->name, search)) return 1;
    if (str_contains_ci(entry->product, search)) return 1;
    if (str_contains_ci(id, search)) return 1;
    if (entry->ds_save_path[0] && str_contains_ci(entry->ds_save_path, search)) return 1;
    return 0;
}

int titles_filtered_count(const TitleList *list, int show_unsupported, const char *search) {
    int seen = 0;
    for (int i = 0; i < list->count; i++) {
        if (!title_entry_visible(&list->items[i], show_unsupported)) continue;
        if (!title_entry_matches(&list->items[i], search)) continue;
        seen++;
    }
    return seen;
}

int titles_filtered_index(const TitleList *list, int slot, int show_unsupported, const char *search) {
    int seen = 0;
    for (int i = 0; i < list->count; i++) {
        if (!title_entry_visible(&list->items[i], show_unsupported)) continue;
        if (!title_entry_matches(&list->items[i], search)) continue;
        if (seen == slot) return i;
        seen++;
    }
    return -1;
}

int titles_find_index(const TitleList *list, u64 title_id) {
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].title_id == title_id) return i;
    }
    return -1;
}

void titles_lookup_name(const TitleList *list, u64 title_id, char *out, size_t out_len) {
    if (!out || out_len < 2) return;
    int idx = titles_find_index(list, title_id);
    if (idx >= 0 && list->items[idx].name[0]) {
        snprintf(out, out_len, "%s", list->items[idx].name);
        return;
    }
    char id[17];
    format_title_id(id, title_id);
    char path[384];
    snprintf(path, sizeof(path), TWODSYNC_ROOT "/state/names.txt");
    FILE *f = fopen(path, "rb");
    if (f) {
        char line[160];
        while (fgets(line, sizeof(line), f)) {
            char *eq = strchr(line, '=');
            if (!eq) continue;
            *eq = 0;
            if (strcasecmp(line, id) == 0) {
                char *name = eq + 1;
                size_t n = strlen(name);
                while (n > 0 && (name[n - 1] == '\n' || name[n - 1] == '\r')) name[--n] = 0;
                snprintf(out, out_len, "%s", name);
                fclose(f);
                return;
            }
        }
        fclose(f);
    }
    if ((title_id >> 32) == 0x00048001ULL) snprintf(out, out_len, "DS Save");
    else snprintf(out, out_len, "Title %s", id);
}

void titles_cache_names(const TitleList *list) {
    char dir[384];
    snprintf(dir, sizeof(dir), TWODSYNC_ROOT "/state");
    mkdir_p(dir);
    char path[384];
    snprintf(path, sizeof(path), "%s/names.txt", dir);
    FILE *f = fopen(path, "wb");
    if (!f) return;
    for (int i = 0; i < list->count; i++) {
        if (!list->items[i].name[0]) continue;
        char id[17];
        format_title_id(id, list->items[i].title_id);
        fprintf(f, "%s=%s\n", id, list->items[i].name);
    }
    fclose(f);
}

#define MAX_SAV_FILES 96

typedef struct SavFile {
    char dir[192];
    char name[96];
    char label[96];
    char product[8];
    int linked;
} SavFile;

static SavFile sav_catalog[MAX_SAV_FILES];
static int sav_catalog_count;

static void name_from_ds_filename(char *out, size_t out_len, const char *filename) {
    snprintf(out, out_len, "%s", filename);
    size_t n = strlen(out);
    if (n > 4 && (strcasecmp(out + n - 4, ".sav") == 0 || strcasecmp(out + n - 4, ".SAV") == 0)) out[n - 4] = 0;
    for (char *p = out; *p; p++) {
        if (*p == '_') *p = ' ';
    }
}

static void normalize_loose(const char *in, char *out, size_t out_len) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 1 < out_len; i++) {
        char c = in[i];
        if (c == '_' || c == '-') c = ' ';
        if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if (c == ' ') {
            while (in[i + 1] == ' ' || in[i + 1] == '_') i++;
            if (j > 0 && out[j - 1] != ' ') out[j++] = ' ';
            continue;
        }
        out[j++] = c;
    }
    out[j] = 0;
}

static int title_index_by_name(const TitleList *list, const char *label) {
    if (!label || !label[0]) return -1;
    char norm[96];
    normalize_loose(label, norm, sizeof(norm));
    if (!norm[0]) return -1;
    for (int i = 0; i < list->count; i++) {
        char other[96];
        normalize_loose(list->items[i].name, other, sizeof(other));
        if (!other[0]) continue;
        if (strcmp(other, norm) == 0) return i;
        if (str_contains_ci(other, norm) || str_contains_ci(norm, other)) return i;
    }
    return -1;
}

static int extract_product_code(const char *filename, char code[5]) {
    if (!filename) return 0;
    size_t len = strlen(filename);
    if (len < 5) return 0;
    const char *dot = strrchr(filename, '.');
    if (!dot || (strcasecmp(dot, ".sav") != 0 && strcasecmp(dot, ".SAV") != 0)) return 0;
    const char *dash = strrchr(filename, '-');
    if (dash && dash + 1 < dot) {
        const char *p = dash + 1;
        while (*p == ' ') p++;
        if ((size_t)(dot - p) >= 4) {
            memcpy(code, p, 4);
            code[4] = 0;
            int valid = 0;
            for (int i = 0; i < 4; i++) {
                char c = code[i];
                if (c == ' ' || c == '.') {
                    code[i] = 0;
                    break;
                }
                if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) valid = 1;
            }
            return valid && code[0];
        }
    }
    return 0;
}

static int sav_catalog_add(const char *dirpath, const char *filename) {
    if (sav_catalog_count >= MAX_SAV_FILES) return -1;
    SavFile *item = &sav_catalog[sav_catalog_count++];
    memset(item, 0, sizeof(*item));
    snprintf(item->dir, sizeof(item->dir), "%s", dirpath);
    snprintf(item->name, sizeof(item->name), "%s", filename);
    name_from_ds_filename(item->label, sizeof(item->label), filename);
    if (strcasecmp(item->label, "main") == 0 || strcasecmp(item->label, "backup") == 0 || strcasecmp(item->label, "restore") == 0 ||
        strlen(item->label) < 3) {
        const char *slash = strrchr(dirpath, '/');
        if (slash && slash[1]) snprintf(item->label, sizeof(item->label), "%s", slash + 1);
    }
    extract_product_code(filename, item->product);
    return 0;
}

static int is_sav_name(const char *name) {
    size_t len = strlen(name);
    if (len < 5) return 0;
    return strcasecmp(name + len - 4, ".sav") == 0 || strcasecmp(name + len - 4, ".SAV") == 0;
}

static void scan_ds_save_dir_depth(TitleList *list, const char *dirpath, int depth, void (*progress)(const char *, int)) {
    if (depth > 8) return;
    DIR *dir = opendir(dirpath);
    if (!dir) return;
    struct dirent *de;
    while ((de = readdir(dir)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        char child[384];
        snprintf(child, sizeof(child), "%s/%s", dirpath, de->d_name);
        struct stat st;
        if (stat(child, &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            scan_ds_save_dir_depth(list, child, depth + 1, progress);
            continue;
        }
        if (!S_ISREG(st.st_mode) || !is_sav_name(de->d_name)) continue;
        if (sav_catalog_add(dirpath, de->d_name) == 0 && progress) progress("Found DS save", -1);
    }
    closedir(dir);
}

static void scan_nintendo_3ds_save_paths(void (*progress)(const char *, int)) {
    static const char *leaf[] = {"Save", "Save data", "Savedata", "SaveData"};
    DIR *root = opendir("sdmc:/Nintendo 3DS");
    if (!root) return;
    struct dirent *id0;
    while ((id0 = readdir(root)) != NULL) {
        if (id0->d_name[0] == '.' || strcasecmp(id0->d_name, "Private") == 0) continue;
        char id0path[320];
        snprintf(id0path, sizeof(id0path), "sdmc:/Nintendo 3DS/%s", id0->d_name);
        struct stat st0;
        if (stat(id0path, &st0) != 0 || !S_ISDIR(st0.st_mode)) continue;
        DIR *id1root = opendir(id0path);
        if (!id1root) continue;
        struct dirent *id1;
        while ((id1 = readdir(id1root)) != NULL) {
            if (id1->d_name[0] == '.') continue;
            char id1path[384];
            snprintf(id1path, sizeof(id1path), "%s/%s", id0path, id1->d_name);
            struct stat st1;
            if (stat(id1path, &st1) != 0 || !S_ISDIR(st1.st_mode)) continue;
            for (size_t k = 0; k < sizeof(leaf) / sizeof(leaf[0]); k++) {
                char savepath[448];
                snprintf(savepath, sizeof(savepath), "%s/Nintendo DS/%s", id1path, leaf[k]);
                if (progress) progress("Scanning DS saves", -1);
                scan_ds_save_dir_depth(NULL, savepath, 0, progress);
            }
        }
        closedir(id1root);
    }
    closedir(root);
}

static void scan_checkpoint_saves(void (*progress)(const char *, int)) {
    static const char *roots[] = {
        "sdmc:/3ds/Checkpoint/saves",
        "sdmc:/3ds/JKSM",
        "sdmc:/_nds/saves",
        "sdmc:/roms/saves",
    };
    for (size_t r = 0; r < sizeof(roots) / sizeof(roots[0]); r++) {
        DIR *dir = opendir(roots[r]);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) != NULL) {
            if (de->d_name[0] == '.') continue;
            char child[384];
            snprintf(child, sizeof(child), "%s/%s", roots[r], de->d_name);
            struct stat st;
            if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode)) continue;
            if (progress) progress("Scanning DS saves", -1);
            scan_ds_save_dir_depth(NULL, child, 0, progress);
        }
        closedir(dir);
    }
}

static void discover_ds_save_roots(void (*progress)(const char *, int)) {
    static const char *known[] = {
        "sdmc:/Nintendo DS/Save",
        "sdmc:/Nintendo DS/Save data",
        "sdmc:/Nintendo DS/Savedata",
        "sdmc:/Nintendo DS/SaveData",
    };
    for (size_t i = 0; i < sizeof(known) / sizeof(known[0]); i++) scan_ds_save_dir_depth(NULL, known[i], 0, progress);
    scan_nintendo_3ds_save_paths(progress);
    scan_checkpoint_saves(progress);
    DIR *root = opendir("sdmc:/");
    if (!root) return;
    struct dirent *de;
    while ((de = readdir(root)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) continue;
        if (!strstr(de->d_name, "DS") && !strstr(de->d_name, "ds")) continue;
        char child[256];
        snprintf(child, sizeof(child), "sdmc:/%s", de->d_name);
        struct stat st;
        if (stat(child, &st) == 0 && S_ISDIR(st.st_mode)) scan_ds_save_dir_depth(NULL, child, 0, progress);
    }
    closedir(root);
}

static int ds_save_exists(const TitleList *list, u64 title_id) {
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].title_id == title_id) return 1;
    }
    return 0;
}

static int title_index_by_product(const TitleList *list, const char *code) {
    if (!code || !code[0]) return -1;
    for (int i = 0; i < list->count; i++) {
        if (strncasecmp(list->items[i].product, code, 4) == 0) return i;
    }
    return -1;
}

static void attach_ds_save(TitleList *list, int index, const char *dirpath, const char *filename) {
    TitleEntry *title = &list->items[index];
    snprintf(title->ds_save_path, sizeof(title->ds_save_path), "%s/%s", dirpath, filename);
    title->has_save = 1;
    snprintf(title->note, sizeof(title->note), "DS save on SD card");
}

static void titles_import_ds_saves(TitleList *list, void (*progress)(const char *, int)) {
    for (int i = 0; i < sav_catalog_count; i++) {
        SavFile *sav = &sav_catalog[i];
        if (sav->linked) continue;
        int index = -1;
        if (sav->product[0]) index = title_index_by_product(list, sav->product);
        if (index < 0) index = title_index_by_name(list, sav->label);
        if (index >= 0) {
            if (!list->items[index].ds_save_path[0]) {
                attach_ds_save(list, index, sav->dir, sav->name);
                sav->linked = 1;
                if (progress) progress("Linked DS save", -1);
            }
            continue;
        }
        if (list->count >= MAX_TITLES) continue;
        u64 tid = title_id_for_ds_save(sav->name);
        if (ds_save_exists(list, tid)) continue;
        TitleEntry *title = &list->items[list->count];
        memset(title, 0, sizeof(*title));
        title->title_id = tid;
        title->media = MEDIATYPE_SD;
        title->kind = TITLE_SUPPORTED_DS_SAVE;
        snprintf(title->ds_save_path, sizeof(title->ds_save_path), "%s/%s", sav->dir, sav->name);
        snprintf(title->name, sizeof(title->name), "%s", sav->label);
        if (sav->product[0]) snprintf(title->product, sizeof(title->product), "%s", sav->product);
        else snprintf(title->product, sizeof(title->product), "DS");
        install_placeholder_icon(title, 0x528A, 0x2A44);
        probe_save(title);
        list->supported_count++;
        list->count++;
        sav->linked = 1;
    }
}

static void scan_ds_saves(TitleList *list, void (*progress)(const char *, int)) {
    sav_catalog_count = 0;
    discover_ds_save_roots(progress);
    titles_import_ds_saves(list, progress);
    for (int i = 0; i < list->count; i++) {
        if (list->items[i].ds_save_path[0]) continue;
        if (list->items[i].kind != TITLE_SUPPORTED_DS_CARD && list->items[i].media != MEDIATYPE_GAME_CARD) continue;
        for (int s = 0; s < sav_catalog_count; s++) {
            SavFile *sav = &sav_catalog[s];
            if (sav->linked) continue;
            int match = 0;
            if (sav->product[0] && strncasecmp(list->items[i].product, sav->product, 4) == 0) match = 1;
            else if (title_index_by_name(list, sav->label) == i) match = 1;
            if (!match) continue;
            attach_ds_save(list, i, sav->dir, sav->name);
            sav->linked = 1;
            probe_save(&list->items[i]);
            break;
        }
    }
}
