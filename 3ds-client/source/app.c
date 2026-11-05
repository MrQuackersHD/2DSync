#include "app.h"
#include "backup.h"
#include "cloud.h"
#include "conflict.h"
#include "http_client.h"
#include "net_policy.h"
#include "sha256.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static AppState *g_app;

static void toast(AppState *app, const char *msg) {
    snprintf(app->toast, sizeof(app->toast), "%s", msg);
    app->toast_frames = 180;
}

static void progress_cb(const char *msg, int pct) {
    if (!g_app) return;
    snprintf(g_app->busy_text, sizeof(g_app->busy_text), "%s", msg);
    g_app->busy_pct = pct;
    g_app->busy = 1;
    if (aptMainLoop()) ui_frame(g_app);
}

static void refresh_online(AppState *app) {
    app->online = network_status() == 3;
}

static void go(AppState *app, Screen screen) {
    app->screen = screen;
    app->cursor = 0;
    app->scroll = 0;
    app->sync_code[0] = 0;
}

static int confirm(AppState *app, const char *title, const char *body) {
    snprintf(app->dialog_title, sizeof(app->dialog_title), "%s", title);
    snprintf(app->dialog_body, sizeof(app->dialog_body), "%s", body);
    snprintf(app->dialog_buttons[0], 24, "Continue");
    snprintf(app->dialog_buttons[1], 24, "Cancel");
    app->dialog_count = 2;
    app->dialog_cursor = 0;
    app->dialog_active = 1;
    int choice = -1;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_DOWN) app->dialog_cursor ^= 1;
        else if (down & KEY_UP) app->dialog_cursor ^= 1;
        touchPosition touch;
        hidTouchRead(&touch);
        if (down & KEY_TOUCH) {
            int row = ui_dialog_slot(touch.py, app->dialog_count);
            if (row >= 0) {
                choice = row;
                break;
            }
        }
        if (down & KEY_A) {
            choice = app->dialog_cursor;
            break;
        }
        if (down & KEY_B) break;
        ui_frame(app);
    }
    app->dialog_active = 0;
    ui_flush_input();
    return choice == 0;
}

static int conflict_dialog(AppState *app, const char *local_sha, unsigned long long local_bytes, const char *cloud_sha, unsigned long long cloud_bytes, const char *cloud_time) {
    char body[220];
    char local_size[24], cloud_size[24];
    format_bytes(local_size, sizeof(local_size), local_bytes);
    format_bytes(cloud_size, sizeof(cloud_size), cloud_bytes);
    snprintf(body, sizeof(body), "Local %s %.8s  Cloud %s %.8s %s", local_size, local_sha, cloud_size, cloud_sha, cloud_time);
    snprintf(app->dialog_title, sizeof(app->dialog_title), "Save Conflict Detected");
    snprintf(app->dialog_body, sizeof(app->dialog_body), "%s", body);
    snprintf(app->dialog_buttons[0], 24, "Keep Local Save");
    snprintf(app->dialog_buttons[1], 24, "Download Cloud Save");
    snprintf(app->dialog_buttons[2], 24, "Upload Local Save");
    snprintf(app->dialog_buttons[3], 24, "Cancel");
    app->dialog_count = 4;
    app->dialog_cursor = 0;
    app->dialog_active = 1;
    int choice = 3;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        if (down & KEY_DOWN) app->dialog_cursor = (app->dialog_cursor + 1) % 4;
        if (down & KEY_UP) app->dialog_cursor = (app->dialog_cursor + 3) % 4;
        touchPosition touch;
        hidTouchRead(&touch);
        if (down & KEY_TOUCH) {
            int row = ui_dialog_slot(touch.py, app->dialog_count);
            if (row >= 0) {
                choice = row;
                break;
            }
        }
        if (down & KEY_A) {
            choice = app->dialog_cursor;
            break;
        }
        if (down & KEY_B) break;
        ui_frame(app);
    }
    app->dialog_active = 0;
    ui_flush_input();
    return choice;
}

static void show_recovery(AppState *app, const char *code) {
    snprintf(app->recovery_shown, sizeof(app->recovery_shown), "%s", code);
    app->recovery_active = 1;
    while (aptMainLoop()) {
        hidScanInput();
        u32 down = hidKeysDown();
        ui_frame(app);
        if (down & (KEY_A | KEY_B)) break;
    }
    app->recovery_active = 0;
}

static TitleEntry *current_title(AppState *app) {
    if (app->title_index < 0 || app->title_index >= app->titles.count) return NULL;
    return &app->titles.items[app->title_index];
}

static int require_supported(AppState *app, TitleEntry **out) {
    TitleEntry *title = current_title(app);
    if (!title || !title_class_supported(title->kind)) {
        toast(app, "This title is not supported");
        return -1;
    }
    *out = title;
    return 0;
}

static void remember_sync(const TitleEntry *title, const char *content, u64 when) {
    char id[17], path[384], body[160];
    format_title_id(id, title->title_id);
    snprintf(path, sizeof(path), TWODSYNC_ROOT "/state");
    mkdir_p(path);
    snprintf(path, sizeof(path), TWODSYNC_ROOT "/state/%s.txt", id);
    int n = snprintf(body, sizeof(body), "content=%s\nunix=%llu\n", content, (unsigned long long)when);
    if (n > 0) write_entire_file(path, body, (size_t)n);
}

static int upload_staged(AppState *app, TitleEntry *title, const char *container_sha, const char *content_sha) {
    char id[17], staged[512], err[160];
    format_title_id(id, title->title_id);
    snprintf(staged, sizeof(staged), TWODSYNC_ROOT "/tmp/%s/current.2dsv", id);
    CloudSaveInfo info;
    app->busy = 1;
    int rc = cloud_upload_container(&app->cfg, id, staged, container_sha, content_sha, &info, err, sizeof(err), progress_cb);
    app->busy = 0;
    if (rc != 0) {
        toast(app, err);
        return -1;
    }
    app->cloud = info;
    app->cloud_checked = 1;
    remember_sync(title, content_sha, unix_time());
    toast(app, "Upload finished");
    return 0;
}

static int download_version(AppState *app, TitleEntry *title, const CloudSaveInfo *info) {
    char id[17], err[180], pre[384];
    format_title_id(id, title->title_id);
    char content[65] = {0}, container[65] = {0};
    if (title->has_save) {
        app->busy = 1;
        int backed = backup_create(title, pre, sizeof(pre), content, container, err, sizeof(err), progress_cb);
        app->busy = 0;
        if (backed != 0) {
            toast(app, err[0] ? err : "Could not back up the local save");
            return -1;
        }
    }
    char dest[384];
    snprintf(dest, sizeof(dest), TWODSYNC_ROOT "/tmp/%s-cloud.2dsv", id);
    app->busy = 1;
    int rc = cloud_download_version(&app->cfg, id, info->version_id, dest, err, sizeof(err), progress_cb);
    app->busy = 0;
    if (rc != 0) {
        toast(app, err);
        return -1;
    }
    uint8_t dig[32];
    char actual[65];
    if (sha256_file(dest, dig) != 0) {
        toast(app, "Could not verify download");
        return -1;
    }
    sha256_hex(dig, actual);
    if (strcmp(actual, info->checksum) != 0) {
        remove(dest);
        toast(app, "Cloud save checksum did not match");
        return -1;
    }
    app->busy = 1;
    rc = backup_restore(title, dest, err, sizeof(err), progress_cb);
    if (rc != 0 && title->has_save && pre[0]) {
        char err2[160];
        backup_restore(title, pre, err2, sizeof(err2), progress_cb);
        snprintf(err, sizeof(err), "Restore failed. Previous save was put back.");
    }
    app->busy = 0;
    if (rc != 0) {
        toast(app, err);
        return -1;
    }
    title->has_save = 1;
    remember_sync(title, info->content_checksum, unix_time());
    toast(app, "Cloud save restored");
    return 0;
}

static void do_upload(AppState *app) {
    TitleEntry *title = NULL;
    if (require_supported(app, &title) != 0) return;
    if (!title->has_save) {
        toast(app, "This game has no local save");
        return;
    }
    char err[180], id[17], content[65], container[65], staged[384];
    format_title_id(id, title->title_id);
    int allowed = cloud_op_allowed(network_status(), config_has_server(&app->cfg), config_has_token(&app->cfg));
    if (allowed != NET_OK) {
        toast(app, net_policy_message(allowed));
        return;
    }
    SaveSnapshot snap;
    app->busy = 1;
    snprintf(app->busy_text, sizeof(app->busy_text), "Reading local save");
    int rc = backup_stage_hash(title, staged, sizeof(staged), content, container, &snap, err, sizeof(err), progress_cb);
    app->busy = 0;
    if (rc != 0) {
        toast(app, err);
        return;
    }
    CloudSaveInfo info;
    memset(&info, 0, sizeof(info));
    app->busy = 1;
    rc = cloud_latest(&app->cfg, id, &info, err, sizeof(err));
    app->busy = 0;
    if (rc != 0) {
        toast(app, err);
        return;
    }
    app->cloud = info;
    app->cloud_checked = 1;
    ConflictKind kind = classify_conflict(1, 1, info.found && strcmp(info.content_checksum, content) == 0);
    if (!info.found) kind = CONFLICT_NO_CLOUD;
    if (kind == CONFLICT_IN_SYNC) {
        toast(app, "Local and cloud saves already match");
        return;
    }
    if (kind == CONFLICT_DIFFER) {
        struct stat st;
        char packed[512];
        snprintf(packed, sizeof(packed), "%s/current.2dsv", staged);
        unsigned long long local_bytes = 0;
        if (stat(packed, &st) == 0) local_bytes = (unsigned long long)st.st_size;
        int choice = conflict_dialog(app, content, local_bytes, info.content_checksum, info.bytes, info.created_at);
        if (choice == 1) {
            download_version(app, title, &info);
            return;
        }
        if (choice != 2) {
            toast(app, choice == 0 ? "Kept the local save" : "Cancelled");
            return;
        }
    }
    upload_staged(app, title, container, content);
}

static void do_download(AppState *app) {
    TitleEntry *title = NULL;
    if (require_supported(app, &title) != 0) return;
    char err[180], id[17];
    format_title_id(id, title->title_id);
    int allowed = cloud_op_allowed(network_status(), config_has_server(&app->cfg), config_has_token(&app->cfg));
    if (allowed != NET_OK) {
        toast(app, net_policy_message(allowed));
        return;
    }
    CloudSaveInfo info;
    app->busy = 1;
    int rc = cloud_latest(&app->cfg, id, &info, err, sizeof(err));
    app->busy = 0;
    if (rc != 0) {
        toast(app, err);
        return;
    }
    app->cloud = info;
    app->cloud_checked = 1;
    if (!info.found) {
        toast(app, "No cloud save for this game");
        return;
    }
    if (!title->has_save) {
        if (confirm(app, "Download Cloud Save", "This game has no local save. Restore the cloud save?")) download_version(app, title, &info);
        return;
    }
    char content[65], container[65], staged[384];
    SaveSnapshot snap;
    app->busy = 1;
    rc = backup_stage_hash(title, staged, sizeof(staged), content, container, &snap, err, sizeof(err), progress_cb);
    app->busy = 0;
    if (rc != 0) {
        toast(app, err);
        return;
    }
    if (strcmp(content, info.content_checksum) == 0) {
        toast(app, "Local and cloud saves already match");
        return;
    }
    struct stat st;
    char packed[512];
    snprintf(packed, sizeof(packed), "%s/current.2dsv", staged);
    unsigned long long local_bytes = 0;
    if (stat(packed, &st) == 0) local_bytes = (unsigned long long)st.st_size;
    int choice = conflict_dialog(app, content, local_bytes, info.content_checksum, info.bytes, info.created_at);
    if (choice == 1) download_version(app, title, &info);
    else if (choice == 2) upload_staged(app, title, container, content);
    else toast(app, choice == 0 ? "Kept the local save" : "Cancelled");
}

static void do_backup(AppState *app) {
    TitleEntry *title = NULL;
    if (require_supported(app, &title) != 0) return;
    if (!title->has_save) {
        toast(app, "This game has no local save");
        return;
    }
    char path[384], content[65], container[65], err[160];
    app->busy = 1;
    int rc = backup_create(title, path, sizeof(path), content, container, err, sizeof(err), progress_cb);
    app->busy = 0;
    toast(app, rc == 0 ? "Local backup stored" : err);
}

static void load_history(AppState *app) {
    TitleEntry *title = current_title(app);
    app->backup_count = 0;
    if (!title) return;
    app->backup_count = backup_list(title->title_id, app->backups, MAX_BACKUPS);
}

static void do_restore_selected(AppState *app) {
    TitleEntry *title = NULL;
    if (require_supported(app, &title) != 0) return;
    if (app->cursor < 0 || app->cursor >= app->backup_count) return;
    if (!confirm(app, "Restore Backup", "The current save is backed up first. Continue?")) return;
    char pre[384] = {0}, content[65], container[65], err[180];
    if (title->has_save) {
        app->busy = 1;
        if (backup_create(title, pre, sizeof(pre), content, container, err, sizeof(err), progress_cb) != 0) {
            app->busy = 0;
            toast(app, "Could not protect the current save");
            return;
        }
    }
    app->busy = 1;
    int rc = backup_restore(title, app->backups[app->cursor].path, err, sizeof(err), progress_cb);
    if (rc != 0 && pre[0]) backup_restore(title, pre, err, sizeof(err), progress_cb);
    app->busy = 0;
    if (rc == 0) {
        title->has_save = 1;
        toast(app, "Backup restored");
    } else toast(app, "Restore failed. The previous save was put back if it existed.");
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

static void open_game(AppState *app) {
    TitleEntry *title = current_title(app);
    if (!title) return;
    titles_load_icon(title);
    app->cloud_checked = 0;
    memset(&app->cloud, 0, sizeof(app->cloud));
    go(app, SCREEN_GAME);
    if (cloud_op_allowed(network_status(), config_has_server(&app->cfg), config_has_token(&app->cfg)) == NET_OK) {
        char id[17], err[120];
        format_title_id(id, title->title_id);
        if (cloud_latest(&app->cfg, id, &app->cloud, err, sizeof(err)) == 0) app->cloud_checked = 1;
    }
}

static void scan_titles(AppState *app) {
    app->busy = 1;
    titles_scan(&app->titles, progress_cb);
    app->busy = 0;
    app->scanned = 1;
    app->title_index = visible_index(app, 0);
    if (app->titles.save_access_failures > 3 && app->titles.supported_count > 0 && app->titles.save_access_failures == app->titles.supported_count) {
        toast(app, "Save archives unavailable. Install the CIA with FBI.");
    }
}

static void sync_action(AppState *app) {
    char err[180];
    if (!config_has_token(&app->cfg)) {
        if (app->cursor == 0) {
            char recovery[40];
            app->busy = 1;
            int rc = cloud_create_account(&app->cfg, recovery, sizeof(recovery), err, sizeof(err));
            app->busy = 0;
            if (rc != 0) toast(app, err);
            else show_recovery(app, recovery);
        } else if (app->cursor == 1) {
            char code[16], request[48], poll[128], status[24];
            if (!ui_prompt("Sync code", code, sizeof(code), 1)) return;
            app->busy = 1;
            int rc = cloud_redeem_code(&app->cfg, code, request, sizeof(request), poll, sizeof(poll), err, sizeof(err));
            app->busy = 0;
            if (rc != 0) {
                toast(app, err);
                return;
            }
            toast(app, "Waiting for the other console to approve");
            for (int i = 0; i < 30; i++) {
                app->busy = 1;
                snprintf(app->busy_text, sizeof(app->busy_text), "Waiting for approval");
                app->busy_pct = (i * 100) / 30;
                rc = cloud_poll(&app->cfg, request, poll, status, sizeof(status), err, sizeof(err));
                ui_frame(app);
                app->busy = 0;
                if (rc != 0) {
                    toast(app, err);
                    return;
                }
                if (strcmp(status, "approved") == 0) {
                    toast(app, "Console paired");
                    return;
                }
                if (strcmp(status, "denied") == 0) {
                    toast(app, "Pairing was denied");
                    return;
                }
                svcSleepThread(1000000000LL);
            }
            toast(app, "Still waiting. Ask the other console to approve.");
        } else if (app->cursor == 2) {
            char code[32], fresh[40];
            if (!ui_prompt("Recovery code", code, sizeof(code), 1)) return;
            app->busy = 1;
            int rc = cloud_recover(&app->cfg, code, fresh, sizeof(fresh), err, sizeof(err));
            app->busy = 0;
            if (rc != 0) toast(app, err);
            else show_recovery(app, fresh);
        }
        return;
    }
    if (app->cursor == 0) {
        int expires = 0;
        app->busy = 1;
        int rc = cloud_create_sync_code(&app->cfg, app->sync_code, sizeof(app->sync_code), &expires, err, sizeof(err));
        app->busy = 0;
        if (rc != 0) toast(app, err);
        else toast(app, "Enter this code on the other console");
    } else if (app->cursor == 1) {
        app->busy = 1;
        app->pending_count = cloud_list_pending(&app->cfg, app->pending, 8, err, sizeof(err));
        app->busy = 0;
        if (app->pending_count < 0) {
            app->pending_count = 0;
            toast(app, err);
            return;
        }
        go(app, SCREEN_PENDING);
    } else if (app->cursor == 2) {
        app->busy = 1;
        app->device_count = cloud_list_devices(&app->cfg, app->devices, 12, err, sizeof(err));
        app->busy = 0;
        if (app->device_count < 0) {
            app->device_count = 0;
            toast(app, err);
            return;
        }
        go(app, SCREEN_DEVICES);
    } else if (app->cursor == 3) {
        char recovery[40];
        app->busy = 1;
        int rc = cloud_rotate_recovery(&app->cfg, recovery, sizeof(recovery), err, sizeof(err));
        app->busy = 0;
        if (rc != 0) toast(app, err);
        else show_recovery(app, recovery);
    } else if (app->cursor == 4) {
        if (!confirm(app, "Unpair This Console", "This console will lose access. Keep the recovery code.")) return;
        app->busy = 1;
        int rc = cloud_revoke(&app->cfg, NULL, 1, err, sizeof(err));
        app->busy = 0;
        if (rc != 0) toast(app, err);
        else {
            app->cfg.device_token[0] = 0;
            app->cfg.account_id[0] = 0;
            config_save(&app->cfg);
            toast(app, "This console was unpaired");
        }
    }
}

static void settings_action(AppState *app) {
    char err[160];
    if (app->cursor == 0) {
        char url[160];
        snprintf(url, sizeof(url), "%s", app->cfg.server_url);
        if (!ui_prompt("https://100.109.119.112", url, sizeof(url), 0)) return;
        if (strncmp(url, "https://", 8) != 0) {
            toast(app, "Server address must start with https://");
            return;
        }
        snprintf(app->cfg.server_url, sizeof(app->cfg.server_url), "%s", url);
        toast(app, config_save(&app->cfg) == 0 ? "Server address saved" : "Could not save settings to the SD card");
    } else if (app->cursor == 1) {
        char name[32];
        snprintf(name, sizeof(name), "%s", app->cfg.console_name);
        if (!ui_prompt("Console name", name, sizeof(name), 0)) return;
        if (strchr(name, '"') || strchr(name, '\\')) {
            toast(app, "Name cannot contain quotes");
            return;
        }
        snprintf(app->cfg.console_name, sizeof(app->cfg.console_name), "%s", name);
        toast(app, config_save(&app->cfg) == 0 ? "Console name saved" : "Could not save settings to the SD card");
    } else if (app->cursor == 2) {
        app->cfg.verify_tls = !app->cfg.verify_tls;
        if (config_save(&app->cfg) != 0) {
            app->cfg.verify_tls = !app->cfg.verify_tls;
            toast(app, "Could not save settings to the SD card");
        } else toast(app, app->cfg.verify_tls ? "TLS verification on" : "TLS verification off");
    } else if (app->cursor == 3) {
        app->busy = 1;
        int rc = cloud_check(&app->cfg, err, sizeof(err));
        app->busy = 0;
        toast(app, err[0] ? err : (rc == 0 ? "Server is reachable" : "Connection failed"));
    } else if (app->cursor == 4) {
        scan_titles(app);
        toast(app, "Game list updated");
    }
}

static int scrolling_list(Screen screen) {
    return screen == SCREEN_GAMES || screen == SCREEN_CLOUD || screen == SCREEN_HISTORY || screen == SCREEN_DEVICES || screen == SCREEN_PENDING;
}

static int move_cursor(AppState *app, int count, u32 down) {
    if (count <= 0) return 0;
    if (down & KEY_DOWN) app->cursor = (app->cursor + 1) % count;
    else if (down & KEY_UP) app->cursor = (app->cursor + count - 1) % count;
    if (down & KEY_TOUCH) {
        touchPosition touch;
        hidTouchRead(&touch);
        int slot = ui_list_slot(touch.py);
        if (slot < 0) return 0;
        int index = scrolling_list(app->screen) ? app->scroll + slot : slot;
        if (index < 0 || index >= count) return 0;
        app->cursor = index;
        return 1;
    }
    return 0;
}

int app_init(AppState *app) {
    memset(app, 0, sizeof(*app));
    g_app = app;
    mkdir_p(TWODSYNC_ROOT "/tmp");
    mkdir_p(TWODSYNC_ROOT "/backups");
    mkdir_p(TWODSYNC_ROOT "/state");
    config_load(&app->cfg);
    config_save(&app->cfg);
    /* NULL is the shared system font. C2D_FontLoadSystem returns NULL
       whenever the requested region matches the console, which is the
       normal case, so it cannot be used as the default font. */
    app->font = NULL;
    app->text = C2D_TextBufNew(4096);
    app->top = C2D_CreateScreenTarget(GFX_TOP, GFX_LEFT);
    app->bottom = C2D_CreateScreenTarget(GFX_BOTTOM, GFX_LEFT);
    app->title_index = -1;
    refresh_online(app);
    if (!app->text || !app->top || !app->bottom) return -1;
    return 0;
}

void app_shutdown(AppState *app) {
    titles_free_icons(&app->titles);
    if (app->text) C2D_TextBufDelete(app->text);
    if (app->font) C2D_FontFree(app->font);
}

int app_frame(AppState *app) {
    g_app = app;
    refresh_online(app);
    if (!app->scanned && !app->busy) scan_titles(app);
    hidScanInput();
    u32 down = hidKeysDown();
    if (app->screen == SCREEN_HOME && (down & KEY_START)) return 0;
    int count = 0;
    switch (app->screen) {
        case SCREEN_HOME: count = 5; break;
        case SCREEN_GAMES: count = visible_count(app); break;
        case SCREEN_GAME: count = 5; break;
        case SCREEN_CLOUD: count = app->cloud_count; break;
        case SCREEN_SYNC: count = config_has_token(&app->cfg) ? 5 : 3; break;
        case SCREEN_HISTORY: count = app->backup_count; break;
        case SCREEN_SETTINGS: count = 5; break;
        case SCREEN_DEVICES: count = app->device_count; break;
        case SCREEN_PENDING: count = app->pending_count; break;
    }
    int touched = count > 0 ? move_cursor(app, count, down) : 0;
    if (app->screen == SCREEN_GAMES && (down & KEY_X)) app->show_unsupported = !app->show_unsupported;
    if (down & KEY_B && app->screen != SCREEN_HOME) {
        if (app->screen == SCREEN_HISTORY) go(app, SCREEN_GAME);
        else if (app->screen == SCREEN_GAME) go(app, SCREEN_GAMES);
        else if (app->screen == SCREEN_PENDING || app->screen == SCREEN_DEVICES) go(app, SCREEN_SYNC);
        else go(app, SCREEN_HOME);
    } else if ((down & KEY_A) || touched) {
        if (app->screen == SCREEN_HOME) {
            if (app->cursor == 0) go(app, SCREEN_GAMES);
            else if (app->cursor == 1) {
                char err[160];
                app->busy = 1;
                int n = cloud_list_saves(&app->cfg, app->cloud_items, 32, err, sizeof(err));
                app->busy = 0;
                app->cloud_count = n < 0 ? 0 : n;
                if (n < 0) toast(app, err);
                go(app, SCREEN_CLOUD);
            } else if (app->cursor == 2) go(app, SCREEN_SYNC);
            else if (app->cursor == 3) {
                load_history(app);
                go(app, SCREEN_HISTORY);
            } else go(app, SCREEN_SETTINGS);
        } else if (app->screen == SCREEN_GAMES && count > 0) {
            int index = visible_index(app, app->cursor);
            if (index >= 0) {
                app->title_index = index;
                titles_load_icon(&app->titles.items[index]);
                open_game(app);
            }
        } else if (app->screen == SCREEN_GAME) {
            if (app->cursor == 0) do_upload(app);
            else if (app->cursor == 1) do_download(app);
            else if (app->cursor == 2) do_backup(app);
            else if (app->cursor == 3) {
                load_history(app);
                if (app->backup_count <= 0) toast(app, "No local backups for this game");
                else {
                    int saved = app->cursor;
                    app->cursor = 0;
                    do_restore_selected(app);
                    app->cursor = saved;
                }
            } else {
                load_history(app);
                go(app, SCREEN_HISTORY);
            }
        } else if (app->screen == SCREEN_HISTORY && ((down & KEY_A) || touched)) do_restore_selected(app);
        else if (app->screen == SCREEN_SYNC && ((down & KEY_A) || touched)) sync_action(app);
        else if (app->screen == SCREEN_SETTINGS && ((down & KEY_A) || touched)) settings_action(app);
        else if (app->screen == SCREEN_PENDING && ((down & KEY_A) || touched) && app->pending_count > 0) {
            char err[160];
            if (!confirm(app, "Pair Console", app->pending[app->cursor].name)) {
                toast(app, "Cancelled");
            } else {
                app->busy = 1;
                int rc = cloud_decide(&app->cfg, app->pending[app->cursor].id, 1, err, sizeof(err));
                app->busy = 0;
                toast(app, rc == 0 ? "Console approved" : err);
                go(app, SCREEN_SYNC);
            }
        } else if (app->screen == SCREEN_DEVICES && ((down & KEY_A) || touched) && app->device_count > 0) {
            if (app->devices[app->cursor].this_console) toast(app, "Use Unpair for this console");
            else if (confirm(app, "Revoke Console", "This console will lose access.")) {
                char err[160];
                app->busy = 1;
                int rc = cloud_revoke(&app->cfg, app->devices[app->cursor].id, 0, err, sizeof(err));
                app->busy = 0;
                toast(app, rc == 0 ? "Console revoked" : err);
                go(app, SCREEN_SYNC);
            }
        }
    }
    ui_frame(app);
    return 1;
}
