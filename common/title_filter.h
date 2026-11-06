#pragma once

#include <stdint.h>

typedef enum TitleClass {
    TITLE_SUPPORTED_GAME = 0,
    TITLE_UNSUPPORTED_SYSTEM = 1,
    TITLE_UNSUPPORTED_DEMO = 2,
    TITLE_UNSUPPORTED_UPDATE = 3,
    TITLE_UNSUPPORTED_DLC = 4,
    TITLE_SUPPORTED_DSIWARE = 5,
    TITLE_SUPPORTED_DS_SAVE = 6,
    TITLE_UNSUPPORTED_OTHER = 7
} TitleClass;

enum {
    TITLE_MEDIA_NAND = 0,
    TITLE_MEDIA_SD = 1,
    TITLE_MEDIA_GAME_CARD = 2
};

TitleClass classify_title(uint64_t title_id, int media_type);
const char *title_class_reason(TitleClass kind);
int title_class_supported(TitleClass kind);
uint64_t title_id_for_ds_save(const char *filename);
