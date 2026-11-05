#include "app.h"
#include "net_policy.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define COL_BG C2D_Color32(14, 18, 28, 255)
#define COL_PANEL C2D_Color32(24, 34, 52, 255)
#define COL_PANEL2 C2D_Color32(18, 26, 40, 255)
#define COL_ACCENT C2D_Color32(62, 198, 255, 255)
#define COL_TEXT C2D_Color32(236, 242, 248, 255)
#define COL_MUTED C2D_Color32(154, 168, 188, 255)
#define COL_OK C2D_Color32(61, 220, 151, 255)
#define COL_WARN C2D_Color32(255, 196, 92, 255)
#define COL_DANGER C2D_Color32(255, 92, 122, 255)
#define COL_LINE C2D_Color32(46, 64, 88, 255)

static void text(AppState *app, float x, float y, float scale, u32 color, const char *str) {
    if (!app->text || !str || !str[0]) return;
    C2D_Text item;
    C2D_TextFontParse(&item, app->font, app->text, str);
    C2D_TextOptimize(&item);
    C2D_DrawText(&item, C2D_WithColor, x, y, 0.0f, scale, scale, color);
}

static void row(float x, float y, float w, float h, int selected) {
    C2D_DrawRectSolid(x, y, 0.0f, w, h, selected ? C2D_Color32(32, 64, 96, 255) : COL_PANEL);
    if (selected) C2D_DrawRectSolid(x, y, 0.0f, 3, h, COL_ACCENT);
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

static void draw_top(AppState *app) {
    C2D_DrawRectSolid(0, 0, 0, 400, 28, COL_PANEL2);
    text(app, 16, 6, 0.55f, COL_ACCENT, "2DSync");
    text(app, 250, 8, 0.42f, app->online ? COL_OK : COL_WARN, app->online ? "Online" : "Offline");
    TitleEntry *title = selected_title(app);
    if (title && title->icon_ready) {
        C2D_Image img = {&title->tex, &title->sub};
        C2D_DrawImageAt(img, 18, 46, 0.0f, NULL, 1.0f, 1.0f);
    } else {
        C2D_DrawRectSolid(18, 46, 0, 48, 48, COL_PANEL);
        text(app, 30, 60, 0.5f, COL_ACCENT, "2D");
    }
    char line[96];
    if (title) {
        trim_copy(line, sizeof(line), title->name, 28);
        text(app, 78, 46, 0.52f, COL_TEXT, line);
        char id[20];
        format_title_id(id, title->title_id);
        snprintf(line, sizeof(line), "%s  %s", id, media_label(title->media));
        text(app, 78, 68, 0.40f, COL_MUTED, line);
        text(app, 78, 86, 0.40f, title->has_save ? COL_OK : COL_WARN, title->has_save ? "Local save found" : title->note);
    } else {
        text(app, 78, 52, 0.55f, COL_TEXT, "Your saves. Your consoles.");
        text(app, 78, 76, 0.42f, COL_MUTED, "Anywhere.");
    }
    C2D_DrawRectSolid(16, 112, 0, 368, 78, COL_PANEL);
    const char *pair = config_has_token(&app->cfg) ? "Paired" : "Not paired";
    snprintf(line, sizeof(line), "Cloud   %s", pair);
    text(app, 28, 122, 0.42f, COL_TEXT, line);
    if (app->cloud_checked && app->cloud.found) {
        snprintf(line, sizeof(line), "Cloud save  %s", app->cloud.created_at);
        text(app, 28, 142, 0.40f, COL_MUTED, line);
        char bytes[32];
        format_bytes(bytes, sizeof(bytes), app->cloud.bytes);
        snprintf(line, sizeof(line), "Size %s", bytes);
        text(app, 28, 160, 0.40f, COL_MUTED, line);
    } else if (app->cloud_checked) {
        text(app, 28, 142, 0.40f, COL_MUTED, "No cloud save for this game");
    } else {
        text(app, 28, 142, 0.40f, COL_MUTED, "Cloud status loads when a game is opened");
    }
    if (!config_has_server(&app->cfg)) text(app, 28, 178, 0.38f, COL_WARN, "Set the server address in Settings");
    else {
        trim_copy(line, sizeof(line), app->cfg.server_url, 42);
        text(app, 28, 178, 0.38f, COL_MUTED, line);
    }
    if (app->toast_frames > 0) {
        C2D_DrawRectSolid(16, 200, 0, 368, 28, COL_PANEL2);
        trim_copy(line, sizeof(line), app->toast, 52);
        text(app, 24, 206, 0.40f, COL_TEXT, line);
    }
}

static void draw_items(AppState *app, const char **labels, int count) {
    text(app, 16, 8, 0.48f, COL_ACCENT, "2DSync");
    for (int i = 0; i < count; i++) {
        float y = 32.0f + i * 34.0f;
        row(12, y, 296, 30, app->cursor == i);
        text(app, 24, y + 7, 0.46f, app->cursor == i ? COL_TEXT : COL_MUTED, labels[i]);
    }
}

static void draw_home(AppState *app) {
    const char *labels[] = {"My Games", "Cloud Saves", "Sync Consoles", "Backup History", "Settings"};
    draw_items(app, labels, 5);
    text(app, 16, 214, 0.36f, COL_MUTED, "A select   START exit");
}

static void draw_games(AppState *app) {
    text(app, 16, 6, 0.46f, COL_TEXT, "My Games");
    text(app, 210, 8, 0.36f, COL_MUTED, app->show_unsupported ? "All titles" : "Supported");
    int count = visible_count(app);
    if (!app->scanned) {
        text(app, 16, 80, 0.46f, COL_MUTED, "Scanning titles...");
        return;
    }
    if (count == 0) {
        text(app, 16, 80, 0.42f, COL_MUTED, "No supported games with titles found.");
        return;
    }
    int window = 5;
    if (app->cursor < app->scroll) app->scroll = app->cursor;
    if (app->cursor >= app->scroll + window) app->scroll = app->cursor - window + 1;
    for (int slot = 0; slot < window; slot++) {
        int vis = app->scroll + slot;
        int index = visible_index(app, vis);
        if (index < 0) break;
        TitleEntry *title = &app->titles.items[index];
        float y = 28.0f + slot * 36.0f;
        row(8, y, 304, 34, app->cursor == vis);
        if (title->icon_ready) {
            C2D_Image img = {&title->tex, &title->sub};
            C2D_DrawImageAt(img, 14, y + 2, 0.0f, NULL, 0.62f, 0.62f);
        }
        char name[64];
        trim_copy(name, sizeof(name), title->name, 24);
        text(app, 50, y + 3, 0.40f, COL_TEXT, name);
        char meta[80];
        snprintf(meta, sizeof(meta), "%s  %s", media_label(title->media), title->has_save ? "Save" : title->note);
        trim_copy(name, sizeof(name), meta, 32);
        text(app, 50, y + 17, 0.34f, title_class_supported(title->kind) ? COL_MUTED : COL_WARN, name);
    }
    text(app, 12, 214, 0.34f, COL_MUTED, "X unsupported   B back");
}

static void draw_game(AppState *app) {
    TitleEntry *title = selected_title(app);
    text(app, 16, 6, 0.44f, COL_TEXT, title ? title->name : "Game");
    const char *labels[] = {"Upload Save", "Download Save", "Backup Locally", "Restore Backup", "View History"};
    for (int i = 0; i < 5; i++) {
        float y = 32.0f + i * 32.0f;
        row(12, y, 296, 28, app->cursor == i);
        text(app, 24, y + 6, 0.44f, COL_TEXT, labels[i]);
    }
    text(app, 16, 200, 0.34f, COL_MUTED, "A run   B back");
    if (title && !title->has_save) text(app, 16, 216, 0.34f, COL_WARN, "No local save archive");
}

static void draw_simple_list(AppState *app, const char *heading, int count, const char *empty, void (*label)(AppState *, int, char *, size_t)) {
    text(app, 16, 6, 0.46f, COL_TEXT, heading);
    if (count <= 0) {
        text(app, 16, 80, 0.40f, COL_MUTED, empty);
        return;
    }
    int window = 5;
    if (app->cursor < app->scroll) app->scroll = app->cursor;
    if (app->cursor >= app->scroll + window) app->scroll = app->cursor - window + 1;
    for (int slot = 0; slot < window; slot++) {
        int index = app->scroll + slot;
        if (index >= count) break;
        float y = 30.0f + slot * 34.0f;
        row(10, y, 300, 30, app->cursor == index);
        char line[80];
        label(app, index, line, sizeof(line));
        text(app, 20, y + 7, 0.40f, COL_TEXT, line);
    }
}

static void cloud_label(AppState *app, int index, char *out, size_t n) {
    snprintf(out, n, "%s  %llu B", app->cloud_items[index].title_id, app->cloud_items[index].bytes);
}

static void hist_label(AppState *app, int index, char *out, size_t n) {
    char bytes[24];
    format_bytes(bytes, sizeof(bytes), app->backups[index].bytes);
    snprintf(out, n, "%s  %s", app->backups[index].label, bytes);
}

static void device_label(AppState *app, int index, char *out, size_t n) {
    snprintf(out, n, "%s%s", app->devices[index].name, app->devices[index].this_console ? "  (this)" : "");
}

static void pending_label(AppState *app, int index, char *out, size_t n) {
    snprintf(out, n, "Approve %s", app->pending[index].name);
}

static void draw_sync(AppState *app) {
    if (!config_has_token(&app->cfg)) {
        const char *labels[] = {"Create Account", "Enter Sync Code", "Recover Account"};
        draw_items(app, labels, 3);
    } else {
        const char *labels[] = {"Show Sync Code", "Pairing Requests", "Paired Consoles", "New Recovery Code", "Unpair This Console"};
        draw_items(app, labels, 5);
    }
    if (app->sync_code[0]) {
        char line[40];
        snprintf(line, sizeof(line), "Code %s", app->sync_code);
        text(app, 16, 210, 0.46f, COL_ACCENT, line);
    }
}

static void draw_settings(AppState *app) {
    const char *labels[] = {"Server Address", "Console Name", "TLS Verification", "Test Connection", "Rescan Games"};
    draw_items(app, labels, 5);
    char line[80];
    snprintf(line, sizeof(line), "TLS %s", app->cfg.verify_tls ? "on" : "off");
    text(app, 200, 108, 0.40f, COL_MUTED, line);
}

static void draw_dialog(AppState *app) {
    C2D_DrawRectSolid(18, 36, 0.2f, 284, 170, C2D_Color32(10, 14, 22, 240));
    C2D_DrawRectSolid(18, 36, 0.2f, 284, 2, COL_ACCENT);
    text(app, 28, 44, 0.46f, COL_WARN, app->dialog_title);
    text(app, 28, 68, 0.36f, COL_TEXT, app->dialog_body);
    for (int i = 0; i < app->dialog_count; i++) {
        float y = 108.0f + i * 22.0f;
        text(app, 36, y, 0.40f, app->dialog_cursor == i ? COL_ACCENT : COL_MUTED, app->dialog_buttons[i]);
    }
}

static void draw_busy(AppState *app) {
    C2D_DrawRectSolid(24, 90, 0.3f, 272, 64, COL_PANEL);
    text(app, 36, 100, 0.42f, COL_TEXT, app->busy_text);
    C2D_DrawRectSolid(36, 126, 0.3f, 248, 8, COL_PANEL2);
    int pct = app->busy_pct;
    if (pct < 0) pct = 30;
    if (pct > 100) pct = 100;
    C2D_DrawRectSolid(36, 126, 0.3f, 248.0f * (pct / 100.0f), 8, COL_ACCENT);
}

static void draw_recovery(AppState *app) {
    C2D_DrawRectSolid(16, 48, 0.2f, 288, 120, COL_PANEL);
    text(app, 28, 58, 0.42f, COL_WARN, "Write this down");
    text(app, 28, 82, 0.50f, COL_ACCENT, app->recovery_shown);
    text(app, 28, 116, 0.36f, COL_TEXT, "It is shown once. A stores it.");
    text(app, 28, 136, 0.36f, COL_MUTED, "This is not your sync code.");
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
    SwkbdState kbd;
    int max_len = (int)out_len - 1;
    if (max_len > 120) max_len = 120;
    swkbdInit(&kbd, digits ? SWKBD_TYPE_WESTERN : SWKBD_TYPE_NORMAL, 2, max_len);
    swkbdSetHintText(&kbd, hint);
    swkbdSetButton(&kbd, SWKBD_BUTTON_LEFT, "Cancel", false);
    swkbdSetButton(&kbd, SWKBD_BUTTON_RIGHT, "OK", true);
    memset(out, 0, out_len);
    SwkbdButton button = swkbdInputText(&kbd, out, out_len);
    return button == SWKBD_BUTTON_RIGHT && out[0];
}
