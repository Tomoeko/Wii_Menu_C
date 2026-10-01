#include "wii_menu/resources/resource_bmg.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { BMG_HEADER_BYTES = 32, FIXTURE_SIZE = 92, INFO_OFFSET = 32, DAT_OFFSET = 64 };

static void put_be16(uint8_t *output, unsigned value) {
    output[0] = (uint8_t)(value >> 8);
    output[1] = (uint8_t)value;
}

static void put_be32(uint8_t *output, unsigned value) {
    output[0] = (uint8_t)(value >> 24);
    output[1] = (uint8_t)(value >> 16);
    output[2] = (uint8_t)(value >> 8);
    output[3] = (uint8_t)value;
}

static void make_fixture(uint8_t data[FIXTURE_SIZE]) {
    memset(data, 0, FIXTURE_SIZE);
    memcpy(data, "MESGbmg1", 8);
    put_be32(data + 8, FIXTURE_SIZE);
    put_be32(data + 12, 2);
    data[16] = 2;
    memcpy(data + INFO_OFFSET, "INF1", 4);
    put_be32(data + INFO_OFFSET + 4, 32);
    put_be16(data + INFO_OFFSET + 8, 2);
    put_be16(data + INFO_OFFSET + 10, 8);
    put_be32(data + INFO_OFFSET + 16, 0);
    put_be32(data + INFO_OFFSET + 20, 0x11223344);
    put_be32(data + INFO_OFFSET + 24, 14);
    put_be32(data + INFO_OFFSET + 28, 0x55667788);
    memcpy(data + DAT_OFFSET, "DAT1", 4);
    put_be32(data + DAT_OFFSET + 4, 28);
    const uint8_t strings[] = {0x00, 'A',  0x00, 0x1a, 0x04, 0x01, 0xd8,
                               0x3d, 0xde, 0x00, 0x00, 'B',  0x00, 0x00,
                               0x00, 'H',  0x00, 'i',  0x00, 0x00};
    memcpy(data + DAT_OFFSET + 8, strings, sizeof(strings));
}

static void test_parser(void) {
    uint8_t data[FIXTURE_SIZE];
    make_fixture(data);
    char error[160] = {0};
    WmBmg *bmg = wm_bmg_parse(data, sizeof(data), error, sizeof(error));
    assert(bmg);
    assert(wm_bmg_count(bmg) == 2);
    assert(strcmp(wm_bmg_text(bmg, 0), "A\xf0\x9f\x98\x80"
                                       "B") == 0);
    assert(strcmp(wm_bmg_message(bmg, 1), "Hi") == 0);
    assert(wm_bmg_text(bmg, 2) == NULL);
    size_t attributes_size = 0;
    const uint8_t *attributes = wm_bmg_attributes(bmg, 1, &attributes_size);
    assert(attributes && attributes_size == 4);
    assert(attributes[0] == 0x55 && attributes[3] == 0x88);
    memset(data, 0, sizeof(data));
    assert(strcmp(wm_bmg_text(bmg, 1), "Hi") == 0);
    wm_bmg_destroy(bmg);

    make_fixture(data);
    data[0] = 'X';
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
    make_fixture(data);
    put_be32(data + 8, FIXTURE_SIZE + 1);
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
    make_fixture(data);
    put_be32(data + INFO_OFFSET + 24, 200);
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
    make_fixture(data);
    data[DAT_OFFSET + 8 + 4] = 2;
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
    make_fixture(data);
    data[DAT_OFFSET + 8 + 8] = 0x00;
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
    make_fixture(data);
    data[DAT_OFFSET + 8 + 18] = 0x00;
    data[DAT_OFFSET + 8 + 19] = 'x';
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
    make_fixture(data);
    put_be16(data + INFO_OFFSET + 10, 12);
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
    make_fixture(data);
    memcpy(data + DAT_OFFSET, "INF1", 4);
    assert(!wm_bmg_parse(data, sizeof(data), error, sizeof(error)));
}

static void test_repeated_message_budget(void) {
    enum { MESSAGE_COUNT = 65, MESSAGE_UNITS = 349525 };
    size_t info_size = 16 + MESSAGE_COUNT * 4;
    size_t dat_size = 8 + MESSAGE_UNITS * 2 + 2;
    size_t dat_offset = BMG_HEADER_BYTES + info_size;
    size_t total_size = dat_offset + dat_size;
    uint8_t *data = calloc(total_size, 1);
    assert(data);

    memcpy(data, "MESGbmg1", 8);
    put_be32(data + 8, (unsigned)total_size);
    put_be32(data + 12, 2);
    data[16] = 2;
    memcpy(data + BMG_HEADER_BYTES, "INF1", 4);
    put_be32(data + BMG_HEADER_BYTES + 4, (unsigned)info_size);
    put_be16(data + BMG_HEADER_BYTES + 8, MESSAGE_COUNT);
    put_be16(data + BMG_HEADER_BYTES + 10, 4);
    memcpy(data + dat_offset, "DAT1", 4);
    put_be32(data + dat_offset + 4, (unsigned)dat_size);
    for (size_t index = 0; index < MESSAGE_UNITS; index++) {
        put_be16(data + dat_offset + 8 + index * 2, 0x0800);
    }

    /* Each record points to the same DAT1 string. Its decoded UTF-8 text
     * occupies exactly 1 MiB including the NUL terminator. */
    char error[160] = {0};
    WmBmg *bmg = wm_bmg_parse(data, total_size, error, sizeof(error));
    assert(!bmg);
    assert(strstr(error, "decoded messages exceed the memory limit") != NULL);
    free(data);
}

static void test_local_export(int argc, char **argv) {
    const char *assets = argc > 1 ? argv[1] : "Files/.local/native-assets";
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/messages/eng/ipl_common.bmg", assets);
    assert(length > 0 && length < (int)sizeof(path));
    FILE *file = fopen(path, "rb");
    if (!file) {
        puts("BMG WAD resource test skipped: local export absent.");
        return;
    }
    fclose(file);
    char error[160] = {0};
    WmBmg *bmg = wm_bmg_load_assets(assets, "ENG", error, sizeof(error));
    assert(bmg);
    assert(wm_bmg_count(bmg) == 458);
    assert(wm_bmg_text(bmg, 157) && wm_bmg_text(bmg, 157)[0]);
    assert(wm_bmg_text(bmg, 170) && wm_bmg_text(bmg, 170)[0]);
    wm_bmg_destroy(bmg);
}

int main(int argc, char **argv) {
    test_parser();
    test_repeated_message_budget();
    test_local_export(argc, argv);
    puts("BMG bounds, UTF-16, controls, and local lookup passed.");
    return 0;
}
