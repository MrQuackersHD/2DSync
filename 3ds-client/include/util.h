#pragma once

#include <3ds.h>
#include <stddef.h>

typedef void (*ProgressFn)(const char *msg, int percent);

#define TWODSYNC_ROOT "sdmc:/3ds/2DSync"

void mkdir_p(const char *path);
void save_utf16_to_utf8(char *out, size_t out_len, const u16 *in);
u64 unix_time(void);
void format_title_id(char out[17], u64 title_id);
void format_bytes(char *out, size_t out_len, u64 bytes);
void format_time(char *out, size_t out_len, u64 unix_time);
int path_join(char *out, size_t out_len, const char *a, const char *b);
void trim_copy(char *dst, size_t dst_len, const char *src, size_t max_chars);
int read_entire_file(const char *path, void *buf, size_t cap, size_t *out_len);
int write_entire_file(const char *path, const void *buf, size_t len);
