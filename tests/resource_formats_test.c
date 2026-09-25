#include "wii_menu/resource_ash.h"
#include "wii_menu/resource_tpl.h"
#include "wii_menu/resource_u8.h"

#include <assert.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void write_be16(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)(value >> 8);
    bytes[1] = (uint8_t)value;
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static void test_u8(void)
{
    uint8_t archive_bytes[134] = {0};
    const uint8_t names[] = "\0folder\0inner\0outer\0";
    memcpy(archive_bytes, "U\xaa" "8-", 4);
    write_be32(archive_bytes + 4, 32);
    write_be32(archive_bytes + 8, 48 + sizeof(names));
    write_be32(archive_bytes + 12, 128);
    write_be32(archive_bytes + 32, 0x01000000);
    write_be32(archive_bytes + 40, 4);
    write_be32(archive_bytes + 44, 0x01000001);
    write_be32(archive_bytes + 52, 3);
    write_be32(archive_bytes + 56, 8);
    write_be32(archive_bytes + 60, 128);
    write_be32(archive_bytes + 64, 3);
    write_be32(archive_bytes + 68, 14);
    write_be32(archive_bytes + 72, 131);
    write_be32(archive_bytes + 76, 3);
    memcpy(archive_bytes + 80, names, sizeof(names));
    memcpy(archive_bytes + 128, "abcxyz", 6);

    char error[128] = {0};
    WmU8Archive archive = {0};
    assert(wm_u8_parse(archive_bytes, sizeof(archive_bytes),
                       &archive, error, sizeof(error)));
    assert(archive.count == 2);
    const WmU8Entry *inner = wm_u8_find(&archive, "folder/inner");
    const WmU8Entry *outer = wm_u8_find(&archive, "outer");
    assert(inner != NULL && inner->size == 3 && memcmp(inner->data, "abc", 3) == 0);
    assert(outer != NULL && outer->size == 3 && memcmp(outer->data, "xyz", 3) == 0);
    wm_u8_free(&archive);

    write_be32(archive_bytes + 52, 5);
    assert(!wm_u8_parse(archive_bytes, sizeof(archive_bytes),
                        &archive, error, sizeof(error)));
    write_be32(archive_bytes + 52, 3);

    /* The second path collides after ASCII case folding. */
    uint8_t duplicate[134] = {0};
    const uint8_t duplicate_names[] = "\0Icon\0icon\0";
    memcpy(duplicate, "U\xaa" "8-", 4);
    write_be32(duplicate + 4, 32);
    write_be32(duplicate + 8, 36 + sizeof(duplicate_names));
    write_be32(duplicate + 12, 128);
    write_be32(duplicate + 32, 0x01000000);
    write_be32(duplicate + 40, 3);
    write_be32(duplicate + 44, 1);
    write_be32(duplicate + 48, 128);
    write_be32(duplicate + 52, 3);
    write_be32(duplicate + 56, 6);
    write_be32(duplicate + 60, 131);
    write_be32(duplicate + 64, 3);
    memcpy(duplicate + 68, duplicate_names, sizeof(duplicate_names));
    memcpy(duplicate + 128, "abcxyz", 6);
    assert(!wm_u8_parse(duplicate, sizeof(duplicate),
                        &archive, error, sizeof(error)));
    assert(strstr(error, "Duplicate") != NULL);
}

static void test_ash(void)
{
    uint8_t literal[] = {
        'A', 'S', 'H', '0', 0, 0, 0, 4, 0, 0, 0, 16,
        0x10, 0x40, 0, 0, 0, 0, 0, 0
    };
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    char error[128] = {0};
    assert(wm_ash_decode(literal, sizeof(literal), &decoded,
                         &decoded_size, error, sizeof(error)));
    assert(decoded_size == 4 && memcmp(decoded, "AAAA", 4) == 0);
    free(decoded);

    /* One literal followed by a length-three back-reference at distance one. */
    uint8_t backref[] = {
        'A', 'S', 'H', '0', 0, 0, 0, 4, 0, 0, 0, 16,
        0x88, 0x28, 0x02, 0, 0, 0
    };
    assert(wm_ash_decode(backref, sizeof(backref), &decoded,
                         &decoded_size, error, sizeof(error)));
    assert(decoded_size == 4 && memcmp(decoded, "AAAA", 4) == 0);
    free(decoded);

    assert(!wm_ash_decode(backref, 13, &decoded, &decoded_size,
                          error, sizeof(error)));
    assert(decoded == NULL && decoded_size == 0);
}

static size_t make_tpl(uint8_t buffer[256], uint32_t format,
                       const uint8_t *tile, size_t tile_size,
                       const uint16_t *palette, size_t palette_count)
{
    memset(buffer, 0, 256);
    write_be32(buffer, 0x0020af30);
    write_be32(buffer + 4, 1);
    write_be32(buffer + 8, 12);
    write_be32(buffer + 12, 20);
    write_be32(buffer + 16, palette != NULL ? 32 : 0);
    write_be16(buffer + 20, 1);
    write_be16(buffer + 22, 1);
    write_be32(buffer + 24, format);
    write_be32(buffer + 28, 96);
    if (palette != NULL) {
        write_be16(buffer + 32, (uint16_t)palette_count);
        write_be32(buffer + 36, 1);
        write_be32(buffer + 40, 44);
        for (size_t index = 0; index < palette_count; index++) {
            write_be16(buffer + 44 + index * 2, palette[index]);
        }
    }
    memcpy(buffer + 96, tile, tile_size);
    return 96 + tile_size;
}

static void test_tpl(void)
{
    uint8_t encoded[256];
    uint8_t tile[64] = {0};
    char error[128] = {0};
    WmTpl tpl = {0};

    tile[0] = 0xa3;
    size_t size = make_tpl(encoded, 2, tile, 32, NULL, 0);
    assert(wm_tpl_decode(encoded, size, &tpl, error, sizeof(error)));
    assert(tpl.count == 1 && tpl.images[0].width == 1 && tpl.images[0].height == 1);
    assert(memcmp(tpl.images[0].rgba, "\x33\x33\x33\xaa", 4) == 0);
    wm_tpl_free(&tpl);

    memset(tile, 0, sizeof(tile));
    write_be16(tile, 0x1020);
    size = make_tpl(encoded, 4, tile, 32, NULL, 0);
    assert(wm_tpl_decode(encoded, size, &tpl, error, sizeof(error)));
    assert(memcmp(tpl.images[0].rgba, "\x10\x04\x00\xff", 4) == 0);
    wm_tpl_free(&tpl);

    memset(tile, 0, sizeof(tile));
    tile[0] = 17;
    tile[1] = 34;
    tile[32] = 51;
    tile[33] = 68;
    size = make_tpl(encoded, 6, tile, 64, NULL, 0);
    assert(wm_tpl_decode(encoded, size, &tpl, error, sizeof(error)));
    assert(memcmp(tpl.images[0].rgba, "\x22\x33\x44\x11", 4) == 0);
    wm_tpl_free(&tpl);

    memset(tile, 0, sizeof(tile));
    write_be16(tile, 0xf800);
    write_be16(tile + 2, 0x001f);
    tile[4] = 0xaa;
    size = make_tpl(encoded, 14, tile, 32, NULL, 0);
    assert(wm_tpl_decode(encoded, size, &tpl, error, sizeof(error)));
    assert(memcmp(tpl.images[0].rgba, "\x9f\x00\x5f\xff", 4) == 0);
    wm_tpl_free(&tpl);

    memset(tile, 0, sizeof(tile));
    tile[0] = 0xa0;
    uint16_t palette[16] = {0};
    palette[10] = 0xf800;
    size = make_tpl(encoded, 8, tile, 32, palette, 16);
    assert(wm_tpl_decode(encoded, size, &tpl, error, sizeof(error)));
    assert(memcmp(tpl.images[0].rgba, "\xff\x00\x00\xff", 4) == 0);
    wm_tpl_free(&tpl);

    write_be16(encoded + 32, 1);
    assert(!wm_tpl_decode(encoded, size, &tpl, error, sizeof(error)));
}

static bool ends_with(const char *value, const char *suffix)
{
    size_t value_size = strlen(value);
    size_t suffix_size = strlen(suffix);
    return value_size >= suffix_size &&
           strcmp(value + value_size - suffix_size, suffix) == 0;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL || fseek(file, 0, SEEK_END) != 0) {
        if (file != NULL) {
            fclose(file);
        }
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *data = malloc((size_t)length != 0 ? (size_t)length : 1);
    if (data == NULL || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static bool probe_resource_archive(const char *path)
{
    size_t input_size = 0;
    uint8_t *input = read_file(path, &input_size);
    if (input == NULL) {
        fprintf(stderr, "Could not read the optional resource archive.\n");
        return false;
    }
    char error[160] = {0};
    WmU8Archive outer = {0};
    if (!wm_u8_parse(input, input_size, &outer, error, sizeof(error))) {
        fprintf(stderr, "Outer resource archive: %s\n", error);
        free(input);
        return false;
    }

    size_t archive_count = 0;
    size_t texture_count = 0;
    uint64_t archive_hash = UINT64_C(14695981039346656037);
    uint64_t texture_hash = UINT64_C(14695981039346656037);
    bool valid = true;
    for (size_t index = 0; index < outer.count && valid; index++) {
        const WmU8Entry *entry = &outer.entries[index];
        if (!ends_with(entry->path, ".ash")) {
            continue;
        }
        uint8_t *decoded = NULL;
        size_t decoded_size = 0;
        if (!wm_ash_decode(entry->data, entry->size, &decoded,
                           &decoded_size, error, sizeof(error))) {
            fprintf(stderr, "ASH decode: %s\n", error);
            valid = false;
            break;
        }
        for (size_t byte = 0; byte < decoded_size; byte++) {
            archive_hash ^= decoded[byte];
            archive_hash *= UINT64_C(1099511628211);
        }
        WmU8Archive inner = {0};
        if (!wm_u8_parse(decoded, decoded_size, &inner, error, sizeof(error))) {
            fprintf(stderr, "Nested resource archive: %s\n", error);
            free(decoded);
            valid = false;
            break;
        }
        archive_count++;
        for (size_t member = 0; member < inner.count && valid; member++) {
            const WmU8Entry *item = &inner.entries[member];
            if (!ends_with(item->path, ".tpl")) {
                continue;
            }
            WmTpl tpl = {0};
            if (!wm_tpl_decode(item->data, item->size,
                               &tpl, error, sizeof(error))) {
                fprintf(stderr, "TPL decode: %s\n", error);
                valid = false;
                break;
            }
            for (size_t image = 0; image < tpl.count; image++) {
                texture_count++;
                size_t bytes = (size_t)tpl.images[image].width *
                               tpl.images[image].height * 4;
                for (size_t byte = 0; byte < bytes; byte++) {
                    texture_hash ^= tpl.images[image].rgba[byte];
                    texture_hash *= UINT64_C(1099511628211);
                }
            }
            wm_tpl_free(&tpl);
        }
        wm_u8_free(&inner);
        free(decoded);
    }
    wm_u8_free(&outer);
    free(input);
    if (valid) {
        printf("Decoded %zu nested archives and %zu textures; "
               "ASH FNV64 %016" PRIx64 "; RGBA FNV64 %016" PRIx64 "\n",
               archive_count, texture_count, archive_hash, texture_hash);
    }
    return valid && archive_count != 0 && texture_count != 0;
}

int main(int argc, char **argv)
{
    test_u8();
    test_ash();
    test_tpl();
    if (argc == 2 && !probe_resource_archive(argv[1])) {
        return 1;
    }
    puts("Resource format tests passed.");
    return 0;
}
