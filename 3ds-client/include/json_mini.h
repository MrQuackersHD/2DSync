#pragma once

#include <stddef.h>

const char *json_find(const char *json, const char *key);
int json_get_string(const char *json, const char *key, char *out, size_t out_len);
int json_get_u64(const char *json, const char *key, unsigned long long *out);
int json_object_at(const char *json, const char *array_key, int index, const char **start, const char **end);
