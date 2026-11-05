#include "icon_tiles.h"

#include <string.h>

void icon_tiles_to_tex64(uint16_t *dst, const uint16_t *src) {
    memset(dst, 0, 64 * 64 * sizeof(uint16_t));
    for (int ty = 0; ty < 6; ty++) {
        for (int tx = 0; tx < 6; tx++) {
            memcpy(dst + (ty * 8 + tx) * 64, src + (ty * 6 + tx) * 64, 64 * sizeof(uint16_t));
        }
    }
}
