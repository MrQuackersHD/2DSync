#pragma once

#include <stdint.h>

#define CONTAINER_MAGIC "2DSV"
#define CONTAINER_VERSION 1
#define CONTAINER_MAX_FILE (8u * 1024u * 1024u)
#define CONTAINER_MAX_TOTAL (32u * 1024u * 1024u)
#define CONTAINER_MAX_FILES 256

typedef struct ContainerInfo {
    uint64_t title_id;
    uint64_t secure_value;
    int has_secure_value;
    int has_extdata;
    uint64_t unix_time;
    uint32_t file_count;
    uint32_t total_bytes;
} ContainerInfo;

/* dir contains savedata/ and optional extdata/. Checksum is hex SHA-256 of the container. */
int container_content_hash(const char *dir, char checksum_hex[65], char *err, unsigned err_len);

int container_pack_dir(const char *dir, const char *out_path, uint64_t title_id, int has_secure,
                       uint64_t secure_value, uint64_t unix_time, char checksum_hex[65], char *err, unsigned err_len);

int container_unpack_dir(const char *in_path, const char *dir, ContainerInfo *info, char *err, unsigned err_len);

int container_read_info(const char *in_path, ContainerInfo *info, char *err, unsigned err_len);
