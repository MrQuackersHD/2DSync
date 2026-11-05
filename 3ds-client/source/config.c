#include "config.h"
#include "util.h"

#include <stdio.h>
#include <string.h>

#define CONFIG_PATH TWODSYNC_ROOT "/config.txt"
#define DEFAULT_SERVER_URL "https://100.109.119.112"

static void set_default(AppConfig *cfg) {
    memset(cfg, 0, sizeof(*cfg));
    snprintf(cfg->server_url, sizeof(cfg->server_url), "%s", DEFAULT_SERVER_URL);
    snprintf(cfg->console_name, sizeof(cfg->console_name), "Nintendo 3DS");
    cfg->verify_tls = 0;
}

static void apply_line(AppConfig *cfg, const char *key, const char *value) {
    if (strcmp(key, "server_url") == 0) {
        if (value[0]) snprintf(cfg->server_url, sizeof(cfg->server_url), "%s", value);
    }
    else if (strcmp(key, "device_token") == 0) snprintf(cfg->device_token, sizeof(cfg->device_token), "%s", value);
    else if (strcmp(key, "device_id") == 0) snprintf(cfg->device_id, sizeof(cfg->device_id), "%s", value);
    else if (strcmp(key, "account_id") == 0) snprintf(cfg->account_id, sizeof(cfg->account_id), "%s", value);
    else if (strcmp(key, "console_name") == 0) snprintf(cfg->console_name, sizeof(cfg->console_name), "%s", value);
    else if (strcmp(key, "console_id") == 0) snprintf(cfg->console_id, sizeof(cfg->console_id), "%s", value);
    else if (strcmp(key, "verify_tls") == 0) cfg->verify_tls = (value[0] == '1');
}

void config_load(AppConfig *cfg) {
    set_default(cfg);
    FILE *f = fopen(CONFIG_PATH, "rb");
    if (!f) {
        config_ensure_identity(cfg);
        return;
    }
    char line[384];
    while (fgets(line, sizeof(line), f)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *value = eq + 1;
        char *nl = strpbrk(value, "\r\n");
        if (nl) *nl = 0;
        apply_line(cfg, line, value);
    }
    fclose(f);
    config_ensure_identity(cfg);
}

int config_save(const AppConfig *cfg) {
    mkdir_p(TWODSYNC_ROOT);
    char body[1024];
    int n = snprintf(body, sizeof(body),
                     "server_url=%s\n"
                     "device_token=%s\n"
                     "device_id=%s\n"
                     "account_id=%s\n"
                     "console_name=%s\n"
                     "console_id=%s\n"
                     "verify_tls=%d\n",
                     cfg->server_url, cfg->device_token, cfg->device_id, cfg->account_id,
                     cfg->console_name, cfg->console_id, cfg->verify_tls ? 1 : 0);
    if (n <= 0 || (size_t)n >= sizeof(body)) return -1;
    return write_entire_file(CONFIG_PATH, body, (size_t)n);
}

int config_has_token(const AppConfig *cfg) {
    return cfg->device_token[0] != 0 && cfg->account_id[0] != 0;
}

int config_has_server(const AppConfig *cfg) {
    return strncmp(cfg->server_url, "https://", 8) == 0 && strlen(cfg->server_url) > 8;
}

void config_ensure_identity(AppConfig *cfg) {
    if (!cfg->console_id[0]) {
        snprintf(cfg->console_id, sizeof(cfg->console_id), "%016llx%08lx",
                 (unsigned long long)osGetTime(), (unsigned long)(svcGetSystemTick() & 0xFFFFFFFF));
        config_save(cfg);
    }
}
