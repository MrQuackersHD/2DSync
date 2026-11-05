#pragma once

#include <stddef.h>
#include <stdint.h>

typedef struct Sha256 {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t buffer[64];
    uint32_t buffer_len;
} Sha256;

void sha256_init(Sha256 *ctx);
void sha256_update(Sha256 *ctx, const void *data, size_t len);
void sha256_final(Sha256 *ctx, uint8_t out[32]);
void sha256_hex(const uint8_t digest[32], char out[65]);
int sha256_file(const char *path, uint8_t out[32]);
