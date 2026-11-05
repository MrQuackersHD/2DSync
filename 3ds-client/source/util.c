#include "util.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

void mkdir_p(const char *path) {
    char buf[512];
    snprintf(buf, sizeof(buf), "%s", path);
    for (char *p = buf + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(buf, 0777);
            *p = '/';
        }
    }
    mkdir(buf, 0777);
}

void save_utf16_to_utf8(char *out, size_t out_len, const u16 *in) {
    size_t o = 0;
    if (!out_len) return;
    for (size_t i = 0; in[i] && o + 4 < out_len; i++) {
        u32 cp = in[i];
        if (cp >= 0xD800 && cp <= 0xDBFF && in[i + 1] >= 0xDC00 && in[i + 1] <= 0xDFFF) {
            cp = 0x10000 + (((cp - 0xD800) << 10) | (in[i + 1] - 0xDC00));
            i++;
        }
        if (cp < 0x80) {
            out[o++] = (char)cp;
        } else if (cp < 0x800) {
            out[o++] = (char)(0xC0 | (cp >> 6));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out[o++] = (char)(0xE0 | (cp >> 12));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            out[o++] = (char)(0xF0 | (cp >> 18));
            out[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            out[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            out[o++] = (char)(0x80 | (cp & 0x3F));
        }
    }
    out[o] = 0;
}

u64 unix_time(void) {
    const u64 ms_1900_to_1970 = 2208988800ULL * 1000ULL;
    u64 ms = osGetTime();
    if (ms < ms_1900_to_1970) return 0;
    return (ms - ms_1900_to_1970) / 1000ULL;
}

void format_title_id(char out[17], u64 title_id) {
    snprintf(out, 17, "%016llX", (unsigned long long)title_id);
}

void format_bytes(char *out, size_t out_len, u64 bytes) {
    if (bytes < 1024) snprintf(out, out_len, "%llu B", (unsigned long long)bytes);
    else if (bytes < 1024 * 1024) snprintf(out, out_len, "%llu KB", (unsigned long long)(bytes / 1024));
    else snprintf(out, out_len, "%llu.%llu MB", (unsigned long long)(bytes / (1024 * 1024)), (unsigned long long)((bytes % (1024 * 1024)) / 102400));
}

void format_time(char *out, size_t out_len, u64 unix_seconds) {
    if (!unix_seconds) {
        snprintf(out, out_len, "Never");
        return;
    }
    u64 days = unix_seconds / 86400ULL;
    u64 sod = unix_seconds % 86400ULL;
    unsigned year = 1970;
    while (1) {
        unsigned diy = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0)) ? 366 : 365;
        if (days < diy) break;
        days -= diy;
        year++;
    }
    static const u8 mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    unsigned month = 1;
    for (int i = 0; i < 12; i++) {
        unsigned dim = mdays[i] + (i == 1 && leap);
        if (days < dim) break;
        days -= dim;
        month++;
    }
    unsigned hour = (unsigned)(sod / 3600ULL);
    unsigned min = (unsigned)((sod % 3600ULL) / 60ULL);
    snprintf(out, out_len, "%04u-%02u-%02u %02u:%02u", year, month, (unsigned)days + 1, hour, min);
}

int path_join(char *out, size_t out_len, const char *a, const char *b) {
    int n = snprintf(out, out_len, "%s/%s", a, b);
    return n > 0 && (size_t)n < out_len;
}

void trim_copy(char *dst, size_t dst_len, const char *src, size_t max_chars) {
    size_t n = 0;
    while (src[n] && n + 1 < dst_len && n < max_chars) {
        dst[n] = src[n];
        n++;
    }
    if (src[n] && n + 3 < dst_len) {
        dst[n++] = '.';
        dst[n++] = '.';
        dst[n++] = '.';
    }
    dst[n] = 0;
}

int read_entire_file(const char *path, void *buf, size_t cap, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(buf, 1, cap, f);
    int extra = fgetc(f);
    fclose(f);
    if (extra != EOF) return -1;
    if (out_len) *out_len = n;
    return 0;
}

int write_entire_file(const char *path, const void *buf, size_t len) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s.partial", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t n = fwrite(buf, 1, len, f);
    if (fflush(f) != 0) {
        fclose(f);
        remove(tmp);
        return -1;
    }
    fclose(f);
    if (n != len) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}
