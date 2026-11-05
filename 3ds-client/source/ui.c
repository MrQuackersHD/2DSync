#include "app.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define COL_BG C2D_Color32(10, 14, 22, 255)
#define COL_PANEL C2D_Color32(22, 30, 46, 255)
#define COL_PANEL2 C2D_Color32(16, 22, 34, 255)
#define COL_ACCENT C2D_Color32(72, 196, 255, 255)
#define COL_TEXT C2D_Color32(244, 247, 252, 255)
#define COL_MUTED C2D_Color32(148, 164, 186, 255)
#define COL_OK C2D_Color32(72, 214, 154, 255)
#define COL_WARN C2D_Color32(255, 196, 92, 255)
#define COL_SELECTED C2D_Color32(28, 58, 88, 255)

#define LIST_Y 30
#define ROW_H 34
#define ROW_STRIDE 38
#define ROW_X 12
#define ROW_W 296
#define VISIBLE_ROWS 5

#define DLG_Y 112
#define DLG_H 24
#define DLG_STRIDE 26

static void text_at(AppState *app, float x, float y, float z, float scale, u32 color, const char *str) {
    if (!app->text || !str || !str[0]) return;
    C2D_Text item;
    C2D_TextFontParse(&item, app->font, app->text, str);
    C2D_TextOptimize(&item);
    C2D_DrawText(&item, C2D_WithColor, x, y, z, scale, scale, color);
}

static void text(AppState *app, float x, float y, float scale, u32 color, const char *str) {
    text_at(app, x, y, 0.0f, scale, color, str);
}

static float row_y(int slot) {
    return (float)(LIST_Y + slot * ROW_STRIDE);
}

int ui_list_slot(int py) {
    if (py < LIST_Y) return -1;
    int slot = (py - LIST_Y) / ROW_STRIDE;
    int within = (py - LIST_Y) % ROW_STRIDE;
    if (slot < 0 || slot >= VISIBLE_ROWS || within >= ROW_H) return -1;
    return slot;
}

int ui_dialog_slot(int py, int count) {
    if (count <= 0 || py < DLG_Y) return -1;
    int slot = (py - DLG_Y) / DLG_STRIDE;
    int within = (py - DLG_Y) % DLG_STRIDE;
    if (slot < 0 || slot >= count || within >= DLG_H) return -1;
    return slot;
}

void ui_flush_input(void) {
    for (int i = 0; i < 45 && aptMainLoop(); i++) {
        hidScanInput();
        if ((hidKeysHeld() & (KEY_A | KEY_B | KEY_TOUCH)) == 0) return;
        gspWaitForVBlank();
    }
}

static void row(float y, int selected) {
    C2D_DrawRectSolid(ROW_X, y, 0.0f, ROW_W, ROW_H, selected ? COL_SELECTED : COL_PANEL);
    C2D_DrawRectSolid(ROW_X, y, 0.0f, 3, ROW_H, selected ? COL_ACCENT : C2D_Color32(38, 52, 72, 255));
}

static void heading(AppState *app, const char *title, const char *aside) {
    text(app, 14, 4, 0.50f, COL_TEXT, title);
    if (aside && aside[0]) text(app, 188, 8, 0.34f, COL_MUTED, aside);
}

static void hint(AppState *app, const char *str) {
    text(app, 14, 222, 0.32f, COL_MUTED, str);
}

static TitleEntry *selected_title(AppState *app) {
    if (app->title_index < 0 || app->title_index >= app->titles.count) return NULL;
    return &app->titles.items[app->title_index];
}

static int visible_index(const AppState *app, int slot) {
    int seen = 0;
    for (int i = 0; i < app->titles.count; i++) {
        if (!app->show_unsupported && !title_class_supported(app->titles.items[i].kind)) continue;
        if (seen == slot) return i;
        seen++;
    }
    return -1;
}

static int visible_count(const AppState *app) {
    int seen = 0;
    for (int i = 0; i < app->titles.count; i++) {
        if (app->show_unsupported || title_class_supported(app->titles.items[i].kind)) seen++;
    }
    return seen;
}

static void draw_icon(TitleEntry *title, float x, float y, float scale) {
    if (!title || !title->icon_ready) {
        float side = 48.0f * scale;
        C2D_DrawRectSolid(x, y, 0, side, side, COL_PANEL2);
        return;
    }
    C2D_Image img = {&title->tex, &title->sub};
    C2D_DrawImageAt(img, x, y, 0.0f, NULL, scale, scale);
}

static void draw_top(AppState *app) {
    C2D_DrawRectSolid(0, 0, 0, 400, 30, COL_PANEL2);
    C2D_DrawRectSolid(0, 30, 0, 400, 2, COL_ACCENT);
    text(app, 16, 6, 0.58f, COL_TEXT, "2DSync");
    text(app, 118, 10, 0.34f, COL_ACCENT, "v0.1.2");
    const char *status = app->online ? "Online" : "Offline";
    text(app, 300, 8, 0.40f, app->online ? COL_OK : COL_WARN, status);

    TitleEntry *title = selected_title(app);
    draw_icon(title, 18, 48, 1.0f);
    char line[96];
    if (title) {
        trim_copy(line, sizeof(line), title->name, 26);
        text(app, 78, 48, 0.52f, COL_TEXT, line);
        char id[20];
        format_title_id(id, title->title_id);
        snprintf(line, sizeof(line), "%s   %s", id, media_label(title->media));
        text(app, 78, 70, 0.38f, COL_MUTED, line);
        text(app, 78, 88, 0.38f, title->has_save ? COL_OK : COL_WARN, title->has_save ? "Save archive on this console" : title->note);
    } else {
        text(app, 78, 56, 0.52f, COL_TEXT, "Your saves. Your consoles.");
        text(app, 78, 80, 0.40f, COL_MUTED, "Anywhere.");
    }

    C2D_DrawRectSolid(16, 114, 0, 368, 86, COL_PANEL);
    const char *pair = config_has_token(&app->cfg) ? "Paired" : "Not paired";
    snprintf(line, sizeof(line), "Cloud    %s", pair);
    text(app, 28, 124, 0.42f, COL_TEXT, line);
    if (app->cloud_checked && app->cloud.found) {
        snprintf(line, sizeof(line), "Latest cloud save   %s", app->cloud.created_at);
        text(app, 28, 146, 0.38f, COL_MUTED, line);
        char bytes[32];
        format_bytes(bytes, sizeof(bytes), app->cloud.bytes);
        snprintf(line, sizeof(line), "Size %s", bytes);
        text(app, 28, 166, 0.38f, COL_MUTED, line);
    } else if (app->cloud_checked) {
        text(app, 28, 146, 0.38f, COL_MUTED, "No cloud save for this game");
    } else {
        text(app, 28, 146, 0.38f, COL_MUTED, "Open a game to check the cloud copy");
    }
    if (config_has_server(&app->cfg)) {
        text(app, 28, 184, 0.40f, COL_TEXT, app->cfg.server_url);
    }
    if (app->toast_frames > 0) {
        C2D_DrawRectSolid(16, 208, 0, 368, 24, COL_PANEL2);
        trim_copy(line, sizeof(line), app->toast, 48);
        text(app, 24, 212, 0.38f, COL_TEXT, line);
    }
}

static void draw_menu(AppState *app, const char *title, const char **labels, const char **values, int count, const char *footer) {
    heading(app, title, NULL);
    for (int i = 0; i < count && i < VISIBLE_ROWS; i++) {
        float y = row_y(i);
        row(y, app->cursor == i);
        text(app, 26, y + 8, 0.44f, app->cursor == i ? COL_TEXT : COL_MUTED, labels[i]);
        if (values && values[i] && values[i][0]) text(app, 168, y + 10, 0.34f, COL_ACCENT, values[i]);
    }
    hint(app, footer);
}

static void draw_home(AppState *app) {
    const char *labels[] = {"My Games", "Cloud Saves", "Sync Consoles", "Backup History", "Settings"};
    draw_menu(app, "Home", labels, NULL, 5, "A  select      START  exit");
}

static void draw_games(AppState *app) {
    heading(app, "My Games", app->show_unsupported ? "All titles" : "Supported");
    int count = visible_count(app);
    if (!app->scanned) {
        text(app, 16, 90, 0.46f, COL_MUTED, "Scanning titles...");
        return;
    }
    if (count == 0) {
        text(app, 16, 90, 0.42f, COL_MUTED, "No supported games found.");
        hint(app, "X  show unsupported      B  back");
        return;
    }
    if (app->cursor < app->scroll) app->scroll = app->cursor;
    if (app->cursor >= app->scroll + VISIBLE_ROWS) app->scroll = app->cursor - VISIBLE_ROWS + 1;
    for (int slot = 0; slot < VISIBLE_ROWS; slot++) {
        int vis = app->scroll + slot;
        int index = visible_index(app, vis);
        if (index < 0) break;
        TitleEntry *title = &app->titles.items[index];
        float y = row_y(slot);
        row(y, app->cursor == vis);
        draw_icon(title, 20, y + 5, 0.5f);
        char name[64];
        trim_copy(name, sizeof(name), title->name, 22);
        text(app, 50, y + 4, 0.40f, COL_TEXT, name);
        char meta[80];
        snprintf(meta, sizeof(meta), "%s   %s", media_label(title->media), title->has_save ? "Save" : "No save");
        text(app, 50, y + 18, 0.32f, title_class_supported(title->kind) ? COL_MUTED : COL_WARN, meta);
    }
    hint(app, "X  unsupported      B  back");
}

static void draw_game(AppState *app) {
    TitleEntry *title = selected_title(app);
    char name[48];
    trim_copy(name, sizeof(name), title ? title->name : "Game", 24);
    const char *labels[] = {"Upload Save", "Download Save", "Backup Locally", "Restore Backup", "View History"};
    draw_menu(app, name, labels, NULL, 5, "A  run      B  back");
}

static void draw_simple_list(AppState *app, const char *title, int count, const char *empty, void (*label)(AppState *, int, char *, size_t)) {
    heading(app, title, NULL);
    if (count <= 0) {
        text(app, 16, 90, 0.42f, COL_MUTED, empty);
        hint(app, "B  back");
        return;
    }
    if (app->cursor < app->scroll) app->scroll = app->cursor;
    if (app->cursor >= app->scroll + VISIBLE_ROWS) app->scroll = app->cursor - VISIBLE_ROWS + 1;
    for (int slot = 0; slot < VISIBLE_ROWS; slot++) {
        int index = app->scroll + slot;
        if (index >= count) break;
        float y = row_y(slot);
        row(y, app->cursor == index);
        char line[80];
        label(app, index, line, sizeof(line));
        text(app, 26, y + 8, 0.40f, COL_TEXT, line);
    }
    hint(app, "A  select      B  back");
}

static void cloud_label(AppState *app, int index, char *out, size_t n) {
    snprintf(out, n, "%s   %llu B", app->cloud_items[index].title_id, app->cloud_items[index].bytes);
}

static void hist_label(AppState *app, int index, char *out, size_t n) {
    char bytes[24];
    format_bytes(bytes, sizeof(bytes), app->backups[index].bytes);
    snprintf(out, n, "%s   %s", app->backups[index].label, bytes);
}

static void device_label(AppState *app, int index, char *out, size_t n) {
    snprintf(out, n, "%s%s", app->devices[index].name, app->devices[index].this_console ? "   this console" : "");
}

static void pending_label(AppState *app, int index, char *out, size_t n) {
    snprintf(out, n, "Approve %s", app->pending[index].name);
}

static void draw_sync(AppState *app) {
    char footer[40];
    const char *hint_text = "A  select      B  back";
    if (app->sync_code[0]) {
        snprintf(footer, sizeof(footer), "Code  %s", app->sync_code);
        hint_text = footer;
    }
    if (!config_has_token(&app->cfg)) {
        const char *labels[] = {"Create Account", "Enter Sync Code", "Recover Account"};
        draw_menu(app, "Sync", labels, NULL, 3, hint_text);
    } else {
        const char *labels[] = {"Show Sync Code", "Pairing Requests", "Paired Consoles", "New Recovery Code", "Unpair This Console"};
        draw_menu(app, "Sync", labels, NULL, 5, hint_text);
    }
}

static void server_host(const char *url, char *out, size_t n) {
    const char *p = url ? url : "";
    if (strncmp(p, "https://", 8) == 0) p += 8;
    snprintf(out, n, "%s", p);
}

static void draw_settings(AppState *app) {
    const char *labels[] = {"Server", "Console Name", "TLS Check", "Test Connection", "Rescan Games"};
    char host[96], name[24];
    server_host(app->cfg.server_url, host, sizeof(host));
    trim_copy(name, sizeof(name), app->cfg.console_name, 14);
    heading(app, "Settings", NULL);
    for (int i = 0; i < 5; i++) {
        float y = row_y(i);
        row(y, app->cursor == i);
        if (i == 0) {
            text(app, 26, y + 2, 0.34f, COL_MUTED, "Server");
            text(app, 26, y + 16, 0.32f, COL_TEXT, host);
        } else {
            text(app, 26, y + 8, 0.44f, app->cursor == i ? COL_TEXT : COL_MUTED, labels[i]);
            const char *value = i == 1 ? name : (i == 2 ? (app->cfg.verify_tls ? "On" : "Off") : "");
            if (value[0]) text(app, 188, y + 10, 0.36f, COL_ACCENT, value);
        }
    }
    hint(app, "A  change      B  back");
}

static void draw_dialog(AppState *app) {
    C2D_DrawRectSolid(14, 28, 0.2f, 292, 184, C2D_Color32(8, 12, 20, 245));
    C2D_DrawRectSolid(14, 28, 0.2f, 292, 2, COL_ACCENT);
    text_at(app, 26, 36, 0.6f, 0.46f, COL_WARN, app->dialog_title);
    text_at(app, 26, 62, 0.6f, 0.34f, COL_TEXT, app->dialog_body);
    for (int i = 0; i < app->dialog_count; i++) {
        float y = (float)(DLG_Y + i * DLG_STRIDE);
        int selected = app->dialog_cursor == i;
        C2D_DrawRectSolid(26, y, 0.3f, 268, DLG_H, selected ? COL_SELECTED : COL_PANEL);
        text_at(app, 36, y + 4, 0.6f, 0.38f, selected ? COL_ACCENT : COL_MUTED, app->dialog_buttons[i]);
    }
}

static void draw_busy(AppState *app) {
    C2D_DrawRectSolid(24, 88, 0.4f, 272, 68, COL_PANEL);
    text_at(app, 36, 98, 0.6f, 0.40f, COL_TEXT, app->busy_text);
    C2D_DrawRectSolid(36, 126, 0.4f, 248, 8, COL_PANEL2);
    int pct = app->busy_pct;
    if (pct < 0) pct = 30;
    if (pct > 100) pct = 100;
    C2D_DrawRectSolid(36, 126, 0.4f, 248.0f * (pct / 100.0f), 8, COL_ACCENT);
}

static void draw_recovery(AppState *app) {
    C2D_DrawRectSolid(16, 48, 0.2f, 288, 120, COL_PANEL);
    text_at(app, 28, 58, 0.6f, 0.42f, COL_WARN, "Write this down");
    text_at(app, 28, 82, 0.6f, 0.50f, COL_ACCENT, app->recovery_shown);
    text_at(app, 28, 116, 0.6f, 0.36f, COL_TEXT, "Shown once. A stores it.");
    text_at(app, 28, 136, 0.6f, 0.36f, COL_MUTED, "This is not your sync code.");
}

void ui_frame(AppState *app) {
    if (app->toast_frames > 0) app->toast_frames--;
    C2D_TextBufClear(app->text);
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    C2D_TargetClear(app->top, COL_BG);
    C2D_SceneBegin(app->top);
    draw_top(app);
    C2D_TargetClear(app->bottom, COL_BG);
    C2D_SceneBegin(app->bottom);
    switch (app->screen) {
        case SCREEN_HOME: draw_home(app); break;
        case SCREEN_GAMES: draw_games(app); break;
        case SCREEN_GAME: draw_game(app); break;
        case SCREEN_CLOUD:
            draw_simple_list(app, "Cloud Saves", app->cloud_count, "No cloud saves yet.", cloud_label);
            break;
        case SCREEN_SYNC: draw_sync(app); break;
        case SCREEN_HISTORY:
            draw_simple_list(app, "Backup History", app->backup_count, "No local backups for this game.", hist_label);
            break;
        case SCREEN_SETTINGS: draw_settings(app); break;
        case SCREEN_DEVICES:
            draw_simple_list(app, "Paired Consoles", app->device_count, "No other consoles.", device_label);
            break;
        case SCREEN_PENDING:
            draw_simple_list(app, "Pairing Requests", app->pending_count, "No consoles are waiting.", pending_label);
            break;
    }
    if (app->recovery_active) draw_recovery(app);
    if (app->dialog_active) draw_dialog(app);
    if (app->busy) draw_busy(app);
    C3D_FrameEnd(0);
}

int ui_prompt(const char *hint, char *out, size_t out_len, int digits) {
    if (!out || out_len < 2) return 0;
    char initial[200];
    snprintf(initial, sizeof(initial), "%s", out);
    SwkbdState kbd;
    int max_len = (int)out_len - 1;
    if (max_len > 180) max_len = 180;
    int initial_len = (int)strlen(initial);
    if (max_len < initial_len) max_len = initial_len;
    swkbdInit(&kbd, digits ? SWKBD_TYPE_WESTERN : SWKBD_TYPE_NORMAL, 2, max_len);
    swkbdSetPasswordMode(&kbd, SWKBD_PASSWORD_NONE);
    swkbdSetValidation(&kbd, SWKBD_ANYTHING, 0, 0);
    swkbdSetFeatures(&kbd, SWKBD_DEFAULT_QWERTY);
    swkbdSetHintText(&kbd, hint);
    swkbdSetInitialText(&kbd, initial);
    swkbdSetButton(&kbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kbd, SWKBD_BUTTON_RIGHT, "OK", true);
    memset(out, 0, out_len);
    SwkbdButton button = swkbdInputText(&kbd, out, out_len);
    ui_flush_input();
    return button == SWKBD_BUTTON_RIGHT && out[0];
}
