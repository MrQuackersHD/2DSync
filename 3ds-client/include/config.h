#pragma once

typedef struct AppConfig {
    char server_url[160];
    char device_token[128];
    char device_id[40];
    char account_id[40];
    char console_name[32];
    char console_id[40];
    int verify_tls;
} AppConfig;

void config_load(AppConfig *cfg);
int config_save(const AppConfig *cfg);
int config_has_token(const AppConfig *cfg);
int config_has_server(const AppConfig *cfg);
void config_ensure_identity(AppConfig *cfg);
