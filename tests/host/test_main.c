#include "conflict.h"
#include "container.h"
#include "icon_tiles.h"
#include "net_policy.h"
#include "sha256.h"
#include "title_filter.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int g_failed = 0;

static void expect(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL: %s\n", msg);
        g_failed++;
    }
}

static void test_sha(void) {
    Sha256 ctx;
    sha256_init(&ctx);
    sha256_update(&ctx, "abc", 3);
    uint8_t out[32];
    sha256_final(&ctx, out);
    char hex[65];
    sha256_hex(out, hex);
    expect(strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") == 0, "sha256 abc");

    sha256_init(&ctx);
    sha256_final(&ctx, out);
    sha256_hex(out, hex);
    expect(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") == 0, "sha256 empty");
}

static void test_titles(void) {
    expect(classify_title(0x0004000000123400ULL, TITLE_MEDIA_SD) == TITLE_SUPPORTED_GAME, "sd game");
    expect(classify_title(0x0004000000123400ULL, TITLE_MEDIA_GAME_CARD) == TITLE_SUPPORTED_GAME, "card game");
    expect(classify_title(0x0004000200123400ULL, TITLE_MEDIA_SD) == TITLE_UNSUPPORTED_DEMO, "demo");
    expect(classify_title(0x0004000E00123400ULL, TITLE_MEDIA_SD) == TITLE_UNSUPPORTED_UPDATE, "update");
    expect(classify_title(0x0004008C00123400ULL, TITLE_MEDIA_SD) == TITLE_UNSUPPORTED_DLC, "dlc");
    expect(classify_title(0x0004001000021300ULL, TITLE_MEDIA_NAND) == TITLE_UNSUPPORTED_SYSTEM, "system");
    expect(classify_title(0x000300044B344445ULL, TITLE_MEDIA_NAND) == TITLE_SUPPORTED_DSIWARE, "dsiware");
    expect(classify_title(0x0004800100ABCDEFULL, TITLE_MEDIA_SD) == TITLE_SUPPORTED_DS_SAVE, "ds save id");
    expect(!title_class_supported(TITLE_UNSUPPORTED_DEMO), "demo not supported");
}

static void test_conflict(void) {
    expect(classify_conflict(1, 0, 0) == CONFLICT_CLOUD_UNKNOWN, "offline unknown");
    expect(classify_conflict(0, 1, 0) == CONFLICT_NO_LOCAL, "cloud only");
    expect(classify_conflict(1, 1, 1) == CONFLICT_IN_SYNC, "same checksum");
    expect(classify_conflict(1, 1, 0) == CONFLICT_DIFFER, "different checksum");
    expect(strcmp(conflict_kind_name(CONFLICT_DIFFER), "differ") == 0, "name");
}

static void test_net(void) {
    expect(cloud_op_allowed(1, 1, 1) == NET_OFFLINE, "disconnected");
    expect(cloud_op_allowed(3, 0, 1) == NET_NO_SERVER, "no server");
    expect(cloud_op_allowed(3, 1, 0) == NET_NOT_PAIRED, "unpaired");
    expect(cloud_op_allowed(3, 1, 1) == NET_OK, "ready");
    expect(strcmp(net_policy_message(NET_OFFLINE), "Internet connection required") == 0, "offline text");
}

static void write_file(const char *path, const char *data) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        perror(path);
        exit(1);
    }
    fwrite(data, 1, strlen(data), f);
    fclose(f);
}

static void test_container(void) {
    mkdir("/tmp/2dsync-test", 0777);
    mkdir("/tmp/2dsync-test/in", 0777);
    mkdir("/tmp/2dsync-test/in/savedata", 0777);
    mkdir("/tmp/2dsync-test/in/savedata/sub", 0777);
    mkdir("/tmp/2dsync-test/in/extdata", 0777);
    write_file("/tmp/2dsync-test/in/savedata/data.bin", "SAVE-ONE");
    write_file("/tmp/2dsync-test/in/savedata/sub/more.bin", "NESTED");
    write_file("/tmp/2dsync-test/in/extdata/ext.bin", "EXT");

    char sum[65];
    char err[128];
    int rc = container_pack_dir("/tmp/2dsync-test/in", "/tmp/2dsync-test/save.2dsv",
                                0x0004000000123400ULL, 1, 0x1122334455667788ULL, 1700000000ULL, sum, err, sizeof(err));
    expect(rc == 0, err[0] ? err : "pack");

    uint8_t dig[32];
    expect(sha256_file("/tmp/2dsync-test/save.2dsv", dig) == 0, "hash file");
    char hex[65];
    sha256_hex(dig, hex);
    expect(strcmp(hex, sum) == 0, "checksum matches file");

    FILE *corrupt = fopen("/tmp/2dsync-test/save.2dsv", "r+b");
    expect(corrupt != NULL, "open packed");
    if (corrupt) {
        fseek(corrupt, -2, SEEK_END);
        fputc('X', corrupt);
        fclose(corrupt);
    }
    uint8_t dig2[32];
    sha256_file("/tmp/2dsync-test/save.2dsv", dig2);
    sha256_hex(dig2, hex);
    expect(strcmp(hex, sum) != 0, "tamper changes checksum");

    rc = container_pack_dir("/tmp/2dsync-test/in", "/tmp/2dsync-test/save.2dsv",
                            0x0004000000123400ULL, 1, 0x1122334455667788ULL, 1700000000ULL, sum, err, sizeof(err));
    expect(rc == 0, "repack");

    mkdir("/tmp/2dsync-test/out", 0777);
    ContainerInfo info;
    rc = container_unpack_dir("/tmp/2dsync-test/save.2dsv", "/tmp/2dsync-test/out", &info, err, sizeof(err));
    expect(rc == 0, err[0] ? err : "unpack");
    expect(info.title_id == 0x0004000000123400ULL, "title id");
    expect(info.has_secure_value == 1, "secure flag");
    expect(info.secure_value == 0x1122334455667788ULL, "secure value");
    expect(info.file_count == 3, "file count");

    FILE *f = fopen("/tmp/2dsync-test/out/savedata/data.bin", "rb");
    char buf[32] = {0};
    if (f) {
        size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        buf[n] = 0;
        fclose(f);
    }
    expect(strcmp(buf, "SAVE-ONE") == 0, "roundtrip data");

    FILE *bad = fopen("/tmp/2dsync-test/bad.2dsv", "wb");
    fwrite("NOPE", 1, 4, bad);
    fclose(bad);
    rc = container_unpack_dir("/tmp/2dsync-test/bad.2dsv", "/tmp/2dsync-test/out2", &info, err, sizeof(err));
    expect(rc != 0, "reject bad magic");

    mkdir("/tmp/2dsync-test/empty", 0777);
    mkdir("/tmp/2dsync-test/empty/savedata", 0777);
    rc = container_pack_dir("/tmp/2dsync-test/empty", "/tmp/2dsync-test/empty.2dsv", 1, 0, 0, 0, sum, err, sizeof(err));
    expect(rc != 0, "reject empty save");
}

static void test_icons(void) {
    uint16_t src[6 * 6 * 64];
    uint16_t dst[64 * 64];
    for (int ty = 0; ty < 6; ty++) {
        for (int tx = 0; tx < 6; tx++) {
            uint16_t *tile = src + (ty * 6 + tx) * 64;
            for (int i = 0; i < 64; i++) tile[i] = (uint16_t)((ty << 8) | (tx << 4) | i);
        }
    }
    icon_tiles_to_tex64(dst, src);
    int copied = 1;
    for (int ty = 0; ty < 6; ty++) {
        for (int tx = 0; tx < 6; tx++) {
            if (memcmp(dst + (ty * 8 + tx) * 64, src + (ty * 6 + tx) * 64, 64 * sizeof(uint16_t)) != 0) copied = 0;
        }
    }
    expect(copied, "smdh tiles land on the matching 64px texture tiles");
    expect(dst[(0 * 8 + 7) * 64] == 0, "right padding stays empty");
    expect(dst[(7 * 8 + 0) * 64] == 0, "bottom padding stays empty");
}

int main(void) {
    test_sha();
    test_titles();
    test_conflict();
    test_net();
    test_container();
    test_icons();
    if (g_failed) {
        fprintf(stderr, "%d test(s) failed\n", g_failed);
        return 1;
    }
    printf("host tests passed\n");
    return 0;
}
