#include "title_filter.h"

TitleClass classify_title(uint64_t title_id, int media_type) {
    uint32_t high = (uint32_t)(title_id >> 32);
    if (high == 0x00040000) {
        (void)media_type;
        return TITLE_SUPPORTED_GAME;
    }
    if (high == 0x00040002) return TITLE_UNSUPPORTED_DEMO;
    if (high == 0x0004000E) return TITLE_UNSUPPORTED_UPDATE;
    if (high == 0x0004008C) return TITLE_UNSUPPORTED_DLC;
    if (high == 0x00030004 || high == 0x00030005 || high == 0x0003000F) return TITLE_SUPPORTED_DSIWARE;
    if (high >= 0x00048000 && high <= 0x0004801F) {
        if (high == 0x00048001) return TITLE_SUPPORTED_DS_SAVE;
        return TITLE_SUPPORTED_DS_CARD;
    }
    if ((high & 0xFFFF0000) == 0x00040000) return TITLE_UNSUPPORTED_SYSTEM;
    return TITLE_UNSUPPORTED_OTHER;
}

const char *title_class_reason(TitleClass kind) {
    switch (kind) {
        case TITLE_SUPPORTED_GAME: return "Supported game";
        case TITLE_UNSUPPORTED_SYSTEM: return "System title";
        case TITLE_UNSUPPORTED_DEMO: return "Demo";
        case TITLE_UNSUPPORTED_UPDATE: return "Update data";
        case TITLE_UNSUPPORTED_DLC: return "DLC";
        case TITLE_SUPPORTED_DSIWARE: return "DSiWare game";
        case TITLE_SUPPORTED_DS_SAVE: return "Nintendo DS save file";
        case TITLE_SUPPORTED_DS_CARD: return "Nintendo DS card game";
        default: return "Unsupported title";
    }
}

int title_class_supported(TitleClass kind) {
    return kind == TITLE_SUPPORTED_GAME || kind == TITLE_SUPPORTED_DSIWARE || kind == TITLE_SUPPORTED_DS_SAVE ||
           kind == TITLE_SUPPORTED_DS_CARD;
}

uint64_t title_id_for_ds_save(const char *filename) {
    uint32_t h = 2166136261u;
    for (const char *p = filename ? filename : ""; *p; p++) {
        h ^= (uint8_t)*p;
        h *= 16777619u;
    }
    return 0x0004800100000000ULL | (uint64_t)h;
}
