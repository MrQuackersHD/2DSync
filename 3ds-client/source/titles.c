#include "titles.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static Result open_icon(Handle *out, u64 title_id, FS_MediaType media, int media_offset) {
    u8 arch[0x10];
    u8 file[0x14];
    memset(arch, 0, sizeof(arch));
    memset(file, 0, sizeof(file));
    memcpy(arch, &title_id, 8);
    arch[media_offset] = (u8)media;
    u32 ncch = 2;
    memcpy(file + 8, &ncch, 4);
    memcpy(file + 12, "icon", 4);
    FS_Path ap = {PATH_BINARY, sizeof(arch), arch};
    FS_Path fp = {PATH_BINARY, sizeof(file), file};
    return FSUSER_OpenFileDirectly(out, ARCHIVE_SAVEDATA_AND_CONTENT, ap, fp, FS_OPEN_READ, 0);
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

static void read_meta(TitleEntry *title, u8 language) {
    Handle file = 0;
    Result rc = open_icon(&file, title->title_id, title->media, 8);
    if (R_FAILED(rc)) rc = open_icon(&file, title->title_id, title->media, 9);
    if (R_FAILED(rc)) {
        snprintf(title->name, sizeof(title->name), "Title %016llX", (unsigned long long)title->title_id);
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
    free(smdh);
}

void titles_load_icon(TitleEntry *title) {
    if (title->icon_ready) return;
    Handle file = 0;
    Result rc = open_icon(&file, title->title_id, title->media, 8);
    if (R_FAILED(rc)) rc = open_icon(&file, title->title_id, title->media, 9);
    if (R_FAILED(rc)) return;
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
    if (!C3D_TexInit(&title->tex, 64, 64, GPU_RGB565)) {
        free(smdh);
        return;
    }
    u16 *dst = (u16 *)title->tex.data;
    memset(dst, 0, 64 * 64 * 2);
    const u16 *src = (const u16 *)(smdh + 0x24C0);
    static const u8 morton[64] = {
        0, 1, 4, 5, 16, 17, 20, 21,
        2, 3, 6, 7, 18, 19, 22, 23,
        8, 9, 12, 13, 24, 25, 28, 29,
        10, 11, 14, 15, 26, 27, 30, 31,
        32, 33, 36, 37, 48, 49, 52, 53,
        34, 35, 38, 39, 50, 51, 54, 55,
        40, 41, 44, 45, 56, 57, 60, 61,
        42, 43, 46, 47, 58, 59, 62, 63
    };
    for (int y = 0; y < 48; y++) {
        for (int x = 0; x < 48; x++) {
            u32 tile = (y / 8) * 8 + (x / 8);
            u32 local = morton[(y & 7) * 8 + (x & 7)];
            dst[tile * 64 + local] = src[y * 48 + x];
        }
    }
    C3D_TexFlush(&title->tex);
    C3D_TexSetFilter(&title->tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&title->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    title->sub.width = 48;
    title->sub.height = 48;
    title->sub.left = 0.0f;
    title->sub.right = 48.0f / 64.0f;
    title->sub.top = 1.0f;
    title->sub.bottom = 1.0f - (48.0f / 64.0f);
    title->icon_ready = 1;
    free(smdh);
}

static void probe_save(TitleEntry *title) {
    if (!title_class_supported(title->kind)) {
        snprintf(title->note, sizeof(title->note), "%s", title_class_reason(title->kind));
        return;
    }
    FS_Archive archive = 0;
    Result rc = open_user_save(&archive, title->media, title->title_id);
    if (R_SUCCEEDED(rc)) {
        title->has_save = 1;
        FSUSER_CloseArchive(archive);
        snprintf(title->note, sizeof(title->note), "Save archive found");
    } else if (title->media == MEDIATYPE_GAME_CARD) {
        snprintf(title->note, sizeof(title->note), "Cartridge save is not a filesystem save");
        title->has_save = 0;
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

static int add_media(TitleList *list, FS_MediaType media, u8 language, void (*progress)(const char *, int)) {
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
        AM_GetTitleProductCode(media, ids[i], title->product);
        title->product[16] = 0;
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

int titles_scan(TitleList *list, void (*progress)(const char *, int)) {
    titles_free_icons(list);
    memset(list, 0, sizeof(*list));
    u8 language = CFG_LANGUAGE_EN;
    CFGU_GetSystemLanguage(&language);
    if (progress) progress("Scanning SD titles", 0);
    if (add_media(list, MEDIATYPE_SD, language, progress) != 0) return -1;
    bool inserted = false;
    if (R_SUCCEEDED(FSUSER_CardSlotIsInserted(&inserted)) && inserted) {
        if (progress) progress("Scanning cartridge", 50);
        if (add_media(list, MEDIATYPE_GAME_CARD, language, progress) != 0) return -1;
    }
    if (progress) progress("Scan complete", 100);
    return 0;
}

const char *media_label(FS_MediaType media) {
    if (media == MEDIATYPE_GAME_CARD) return "Cartridge";
    if (media == MEDIATYPE_SD) return "Digital";
    return "NAND";
}
