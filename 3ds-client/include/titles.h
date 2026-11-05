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
int titles_scan(TitleList *list, void (*progress)(const char *, int));
void titles_load_icon(TitleEntry *title);
const char *media_label(FS_MediaType media);
