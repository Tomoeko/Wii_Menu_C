#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "wii_menu/resources/resource_font.h"
#include "wii_menu/fonts/shared_font_export.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    FONT_SIZE = 161,
    ARCHIVE_SIZE = 128 + FONT_SIZE * 2,
    FINF = 16,
    TGLP = 48,
    CWDH = 112,
    CMAP = 131
};

static void be16(uint8_t *target, uint16_t value) {
    target[0] = (uint8_t)(value >> 8);
    target[1] = (uint8_t)value;
}

static void be32(uint8_t *target, uint32_t value) {
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static void make_font(uint8_t bytes[FONT_SIZE]) {
    memset(bytes, 0, FONT_SIZE);
    memcpy(bytes, "RFNA", 4);
    bytes[4] = 0xfe;
    bytes[5] = 0xff;
    be32(bytes + 8, FONT_SIZE);
    be16(bytes + 12, 16);
    be16(bytes + 14, 4);

    memcpy(bytes + FINF, "FINF", 4);
    be32(bytes + FINF + 4, 32);
    bytes[FINF + 9] = 4;
    bytes[FINF + 15] = 1;
    be32(bytes + FINF + 16, TGLP + 8);
    be32(bytes + FINF + 20, CWDH + 8);
    be32(bytes + FINF + 24, CMAP + 8);
    bytes[FINF + 28] = 4;
    bytes[FINF + 29] = 4;
    bytes[FINF + 30] = 3;

    memcpy(bytes + TGLP, "TGLP", 4);
    be32(bytes + TGLP + 4, 64);
    bytes[TGLP + 8] = 3;
    bytes[TGLP + 9] = 3;
    bytes[TGLP + 10] = 2;
    bytes[TGLP + 11] = 3;
    be32(bytes + TGLP + 12, 32);
    be16(bytes + TGLP + 16, 1);
    be16(bytes + TGLP + 18, 0);
    be16(bytes + TGLP + 20, 1);
    be16(bytes + TGLP + 22, 1);
    be16(bytes + TGLP + 24, 8);
    be16(bytes + TGLP + 26, 8);
    be32(bytes + TGLP + 28, TGLP + 32);
    memset(bytes + TGLP + 32, 0xff, 32);

    memcpy(bytes + CWDH, "CWDH", 4);
    be32(bytes + CWDH + 4, 19);
    bytes[CWDH + 16] = 1;
    bytes[CWDH + 17] = 2;
    bytes[CWDH + 18] = 3;

    memcpy(bytes + CMAP, "CMAP", 4);
    be32(bytes + CMAP + 4, 30);
    be16(bytes + CMAP + 8, 'C');
    be16(bytes + CMAP + 10, 'P');
    be16(bytes + CMAP + 12, 2);
    be16(bytes + CMAP + 20, 2);
    be16(bytes + CMAP + 22, 'C');
    be16(bytes + CMAP + 24, 0);
    be16(bytes + CMAP + 26, 'P');
    be16(bytes + CMAP + 28, 0);
}

static void make_archive(uint8_t bytes[ARCHIVE_SIZE]) {
    memset(bytes, 0, ARCHIVE_SIZE);
    memcpy(bytes,
           "U\xaa"
           "8-",
           4);
    be32(bytes + 4, 32);
    be32(bytes + 8, 59);
    be32(bytes + 12, 128);
    be32(bytes + 32, 0x01000000);
    be32(bytes + 40, 3);
    be32(bytes + 44, 1);
    be32(bytes + 48, 128);
    be32(bytes + 52, FONT_SIZE);
    be32(bytes + 56, 12);
    be32(bytes + 60, 128 + FONT_SIZE);
    be32(bytes + 64, FONT_SIZE);
    memcpy(bytes + 68, "\0wbf1.brfna\0wbf2.brfna\0", 23);
    make_font(bytes + 128);
    make_font(bytes + 128 + FONT_SIZE);
}

static void check_alias(const char *directory, const char *name,
                        const uint8_t *expected) {
    char path[1200];
    int length = snprintf(path, sizeof(path), "%s/%s", directory, name);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "rb");
    assert(file);
    uint8_t actual[FONT_SIZE];
    assert(fread(actual, 1, sizeof(actual), file) == sizeof(actual));
    assert(fgetc(file) == EOF);
    assert(fclose(file) == 0);
    assert(memcmp(actual, expected, sizeof(actual)) == 0);
    WmFont *font = wm_font_decode(actual, sizeof(actual), NULL, 0);
    assert(font);
    const WmFontGlyph *c = wm_font_glyph(font, 'C');
    const WmFontGlyph *p = wm_font_glyph(font, 'P');
    assert(c && c->width == 2 && p && p->width == 2);
    wm_font_destroy(font);
    assert(remove(path) == 0);
}

int main(void) {
    const char *temporary = getenv("TMPDIR");
    if (!temporary || !temporary[0])
        temporary = "/tmp";
    char root[1024];
    int length =
        snprintf(root, sizeof(root), "%s/wm-shared-font-test-XXXXXX", temporary);
    assert(length > 0 && (size_t)length < sizeof(root));
    assert(mkdtemp(root));
    uint8_t archive[ARCHIVE_SIZE];
    make_archive(archive);
    char error[160];
    archive[68 + 12 + 3] = 'x';
    assert(
        !wm_shared_font_export(archive, sizeof(archive), root, error, sizeof(error)));
    assert(strstr(error, "wbf1") != NULL);
    make_archive(archive);

    char directory[1152];
    length = snprintf(directory, sizeof(directory), "%s/fonts", root);
    assert(length > 0 && (size_t)length < sizeof(directory));
    char outside[1152];
    length = snprintf(outside, sizeof(outside), "%s-outside", root);
    assert(length > 0 && (size_t)length < sizeof(outside));
    assert(mkdir(outside, 0700) == 0);
    char *outside_canonical = realpath(outside, NULL);
    assert(outside_canonical);
    assert(symlink(outside_canonical, directory) == 0);
    free(outside_canonical);
    assert(
        !wm_shared_font_export(archive, sizeof(archive), root, error, sizeof(error)));
    assert(unlink(directory) == 0);
    assert(rmdir(outside) == 0);

    char linked_root[1152], linked_slash[1152], linked_dot[1152];
    length = snprintf(linked_root, sizeof(linked_root), "%s-linked", root);
    assert(length > 0 && (size_t)length < sizeof(linked_root));
    length = snprintf(linked_slash, sizeof(linked_slash), "%s/", linked_root);
    assert(length > 0 && (size_t)length < sizeof(linked_slash));
    length = snprintf(linked_dot, sizeof(linked_dot), "%s/.", linked_root);
    assert(length > 0 && (size_t)length < sizeof(linked_dot));
    char *root_canonical = realpath(root, NULL);
    assert(root_canonical);
    assert(symlink(root_canonical, linked_root) == 0);
    free(root_canonical);
    assert(!wm_shared_font_export(archive, sizeof(archive), linked_root, error,
                                  sizeof(error)));
    assert(!wm_shared_font_export(archive, sizeof(archive), linked_slash, error,
                                  sizeof(error)));
    assert(!wm_shared_font_export(archive, sizeof(archive), linked_dot, error,
                                  sizeof(error)));
    assert(unlink(linked_root) == 0);

    if (!wm_shared_font_export(archive, sizeof(archive), root, error, sizeof(error))) {
        fprintf(stderr, "valid fixture export failed: %s\n", error);
        return 1;
    }
    static const char *const aliases[] = {"wbf1.brfna",
                                          "RevoIpl_RodinNTLGPro_DB_32_I4.brfnt",
                                          "WiiBitmapFontType1.brfnt",
                                          "wbf2.brfna",
                                          "RevoIpl_UtrilloProGrecoStd_M_32_I4.brfnt",
                                          "WiiBitmapFontType2.brfnt"};
    for (size_t index = 0; index < 6; index++) {
        check_alias(directory, aliases[index],
                    archive + 128 + (index >= 3 ? FONT_SIZE : 0));
    }
    assert(rmdir(directory) == 0);
    assert(rmdir(root) == 0);
    return 0;
}
