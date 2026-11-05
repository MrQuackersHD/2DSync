#pragma once

#include <stdint.h>

/* SMDH large icons are 48x48 RGB565, packed as 8x8 Morton tiles, 6 tiles
   across. A citro2d texture is 64x64, which is 8 tiles across, with the
   same Morton order inside each tile. */
void icon_tiles_to_tex64(uint16_t *dst, const uint16_t *src);
