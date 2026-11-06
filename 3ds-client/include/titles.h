#pragma once

#include "title_filter.h"

#include <3ds.h>
#include <citro3d.h>
#include <tex3ds.h>

#define MAX_TITLES 256

typedef struct TitleEntry {
    u64 title_id;
    FS_MediaType media;
    u16 version;
    TitleClass kind;
    char name[96];
    char product[20];
    int has_save;
    int has_extdata;
    u64 extdata_id;
    char note[72];
    char ds_save_path[128];
    C3D_Tex tex;
    Tex3DS_SubTexture sub;
    int icon_ready;
} TitleEntry;

typedef struct TitleList {
    TitleEntry items[MAX_TITLES];
    int count;
    int supported_count;
    int save_access_failures;
} TitleList;

void titles_free_icons(TitleList *list);
int titles_scan(TitleList *list, int include_unsupported, void (*progress)(const char *, int));
void titles_load_icon(TitleEntry *title);
const char *media_label(FS_MediaType media);
int title_entry_visible(const TitleEntry *entry, int show_unsupported);
int title_entry_matches(const TitleEntry *entry, const char *search);
int titles_filtered_count(const TitleList *list, int show_unsupported, const char *search);
int titles_filtered_index(const TitleList *list, int slot, int show_unsupported, const char *search);
void titles_cache_names(const TitleList *list);
void titles_lookup_name(const TitleList *list, u64 title_id, char *out, size_t out_len);
int titles_find_index(const TitleList *list, u64 title_id);
