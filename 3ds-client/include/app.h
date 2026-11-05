#pragma once

#include "backup.h"
#include "cloud.h"
#include "config.h"
#include "titles.h"

#include <citro2d.h>

typedef enum Screen {
    SCREEN_HOME = 0,
    SCREEN_GAMES,
    SCREEN_GAME,
    SCREEN_CLOUD,
    SCREEN_SYNC,
    SCREEN_HISTORY,
    SCREEN_SETTINGS,
    SCREEN_DEVICES,
    SCREEN_PENDING
} Screen;

typedef struct AppState {
    Screen screen;
    int cursor;
    int scroll;
    char toast[180];
    int toast_frames;
    int busy;
    char busy_text[96];
    int busy_pct;
    TitleList titles;
    int title_index;
    int show_unsupported;
    CloudSaveInfo cloud;
    int cloud_checked;
    LocalBackup backups[MAX_BACKUPS];
    int backup_count;
    RemoteDevice devices[12];
    int device_count;
    PairPending pending[8];
    int pending_count;
    CloudListItem cloud_items[32];
    int cloud_count;
    char sync_code[16];
    char recovery_shown[40];
    int recovery_active;
    AppConfig cfg;
    int online;
    int scanned;
    char dialog_title[64];
    char dialog_body[220];
    char dialog_buttons[4][24];
    int dialog_count;
    int dialog_cursor;
    int dialog_active;
    C3D_RenderTarget *top;
    C3D_RenderTarget *bottom;
    C2D_Font font;
    C2D_TextBuf text;
} AppState;

int app_init(AppState *app);
void app_shutdown(AppState *app);
int app_frame(AppState *app);
void ui_frame(AppState *app);
int ui_prompt(const char *hint, char *out, size_t out_len, int digits);
int ui_list_slot(int py);
int ui_dialog_slot(int py, int count);
void ui_flush_input(void);
