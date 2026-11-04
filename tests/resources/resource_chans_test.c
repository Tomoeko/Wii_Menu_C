#include "console_common/support/host.h"
#include "wii_menu/animation/channel_animation.h"
#include "wii_menu/resources/resource_chans.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Authored RCHE program, with synthetic text and pane data. No original
 * channel bytes or translations are needed by the portable tests. */
typedef struct Fixture {
    uint8_t bytes[4096];
    size_t cursor;
    size_t setup;
    size_t title;
} Fixture;

static void put16(uint8_t *bytes, size_t offset, unsigned value) {
    bytes[offset] = (uint8_t)(value >> 8);
    bytes[offset + 1] = (uint8_t)value;
}

static void put32(uint8_t *bytes, size_t offset, size_t value) {
    bytes[offset] = (uint8_t)(value >> 24);
    bytes[offset + 1] = (uint8_t)(value >> 16);
    bytes[offset + 2] = (uint8_t)(value >> 8);
    bytes[offset + 3] = (uint8_t)value;
}

static void byte(Fixture *fixture, unsigned value) {
    assert(fixture->cursor < 0x300);
    fixture->bytes[fixture->cursor++] = (uint8_t)value;
}

static void indexed(Fixture *fixture, unsigned opcode, unsigned index) {
    byte(fixture, opcode);
    byte(fixture, index);
}

static void member(Fixture *fixture, unsigned opcode, unsigned index, unsigned count) {
    byte(fixture, opcode);
    byte(fixture, index >> 8);
    byte(fixture, index);
    if (opcode == 0x30)
        byte(fixture, count);
}

static void patch_jump(Fixture *fixture, size_t at, size_t target, unsigned opcode) {
    assert(target >= at + 2 && target - at - 2 < 4096);
    size_t distance = target - at - 2;
    fixture->bytes[at] = (uint8_t)(opcode | (distance >> 8));
    fixture->bytes[at + 1] = (uint8_t)distance;
}

static void names(uint8_t *bytes, size_t table, const char *const *values,
                  size_t count) {
    size_t cursor = table + count * 4;
    for (size_t index = 0; index < count; index++) {
        size_t length = strlen(values[index]);
        put32(bytes, table + index * 4, (length << 24) | (cursor - table));
        memcpy(bytes + cursor, values[index], length);
        cursor += length;
    }
}

static void make_fixture(Fixture *fixture) {
    memset(fixture, 0, sizeof(*fixture));
    memcpy(fixture->bytes, "RCHE\x03\0\x01\0", 8);
    put32(fixture->bytes, 8, sizeof(fixture->bytes));
    put32(fixture->bytes, 0x30, 0x60);
    fixture->cursor = 0x81;
    indexed(fixture, 0x18, 12);
    indexed(fixture, 0x60, 4);
    byte(fixture, 0);
    fixture->setup = fixture->cursor;
    member(fixture, 0x2c, 3, 0);
    byte(fixture, 4);
    indexed(fixture, 0x5f, 0xff);
    member(fixture, 0x30, 4, 1);
    indexed(fixture, 0x7f, 0xfd);
    indexed(fixture, 0x40, 0);
    member(fixture, 0x30, 0, 0);
    byte(fixture, 4);
    size_t branches[3];
    for (unsigned choice = 0; choice < 3; choice++) {
        indexed(fixture, 0x40, 0);
        member(fixture, 0x32, choice + 1, 0);
        branches[choice] = fixture->cursor;
        indexed(fixture, 0x80, 0);
    }
    byte(fixture, 5);
    size_t fallback = fixture->cursor;
    indexed(fixture, 0xe0, 0);
    for (unsigned choice = 0; choice < 3; choice++) {
        patch_jump(fixture, branches[choice], fixture->cursor, 0x80);
        if (!choice)
            patch_jump(fixture, fallback, fixture->cursor, 0xe0);
        indexed(fixture, 0x5f, 0xfd);
        byte(fixture, 4);
        indexed(fixture, 0x18, 12 + choice);
        byte(fixture, 4);
        indexed(fixture, 0x18, 20);
        byte(fixture, 4);
        member(fixture, 0x2c, choice, 0);
        byte(fixture, 4);
        indexed(fixture, 0x40, 2);
        indexed(fixture, 0x31, 4);
        byte(fixture, 0);
    }
    fixture->title = fixture->cursor;
    static const char *const recipe[] = {
        "GetFont",      "SetFont",      "SetFontSize",  "GetLineSpace",
        "SetLineSpace", "GetCharSpace", "SetCharSpace", "CalcStringRect",
        "SetFontSize",  "GetWidth",     "GetHeight",    "SetSize",
        "SetString",    "GetString"};
    static const unsigned arguments[] = {0, 1, 2, 0, 1, 0, 1, 2, 2, 0, 0, 2, 1, 0};
    const char *members[21] = {"SCGetLanguage",
                               "SC_LANG_ENGLISH",
                               "SC_LANG_FRENCH",
                               "SC_LANG_SPANISH",
                               "FindPaneByName",
                               "show",
                               "hide"};
    for (unsigned index = 0; index < 14; index++) {
        members[index + 7] = recipe[index];
        member(fixture, 0x30, index + 7, arguments[index]);
    }
    byte(fixture, 0);
    put32(fixture->bytes, 0x2c, fixture->cursor - 0x80);
    put32(fixture->bytes, 0x40, 2);
    put32(fixture->bytes, 0x44, 0x300 - 0x20);
    put32(fixture->bytes, 0x300, fixture->setup - 0x80);
    put16(fixture->bytes, 0x304, 1);
    fixture->bytes[0x306] = 1;
    put32(fixture->bytes, 0x308, fixture->title - 0x80);
    put16(fixture->bytes, 0x30c, 2);
    fixture->bytes[0x30e] = 4;
    put32(fixture->bytes, 0x48, 21);
    put32(fixture->bytes, 0x4c, 0x400 - 0x20);
    names(fixture->bytes, 0x400, members, 21);
    put32(fixture->bytes, 0x34, 5);
    put32(fixture->bytes, 0x60, 0xb00 - 0x20);
    static const char *const symbols[] = {"System", "setRegionPanes", "setTitle",
                                          "debugPrint", "size"};
    names(fixture->bytes, 0xb00, symbols, 5);
    static const char *const texts[] = {"Test English\ntext", "Test French\ntext",
                                        "Test Spanish\ntext", "txt_green"};
    put32(fixture->bytes, 0x50, 4);
    put32(fixture->bytes, 0x54, 0x900 - 0x20);
    size_t cursor = 0x900;
    for (size_t index = 0; index < 4; index++) {
        size_t length = strlen(texts[index]);
        put16(fixture->bytes, cursor, (unsigned)(length * 2));
        cursor += 2;
        for (size_t character = 0; character < length; character++) {
            put16(fixture->bytes, cursor, (unsigned char)texts[index][character]);
            cursor += 2;
        }
    }
}

static void expect_invalid(const Fixture *fixture) {
    WmChansLanguages result = {0};
    result.count = 99;
    char error[160];
    assert(!wm_chans_languages_decode(fixture->bytes, sizeof(fixture->bytes), &result,
                                      error, sizeof(error)));
    assert(result.count == 99 && error[0]);
}

static bool measure_rect(void *context, const WmLayout *layout,
                         const WmLayoutPaneState *pane, float size[2]) {
    (void)layout;
    unsigned *calls = context;
    (*calls)++;
    assert(pane->font_size[1] == 20 && strstr(pane->text, "Test "));
    size[0] = 123;
    size[1] = 45;
    return true;
}

static void test_pose(const Fixture *fixture, const char *layout_path) {
    char companion[4096];
    int written = snprintf(companion, sizeof(companion), "%s.cs", layout_path);
    assert(written > 0 && (size_t)written < sizeof(companion));
    FILE *file = cc_host_fopen(companion, "wb");
    assert(file);
    assert(fwrite(fixture->bytes, 1, sizeof(fixture->bytes), file) ==
           sizeof(fixture->bytes));
    assert(fclose(file) == 0);
    char error[160];
    WmLayout *layout = wm_layout_load_json(layout_path, error, sizeof(error));
    assert(layout && wm_layout_channel_languages(layout)->count == 3);
    unsigned calls = 0;
    WmChannelAnimationOptions options = {.measure_rect = measure_rect,
                                         .measure_context = &calls};
    static const char *const languages[] = {"ENG", "FRA", "SPA", "JPN"};
    static const char *const texts[] = {"Test English\ntext", "Test French\ntext",
                                        "Test Spanish\ntext", "Test English\ntext"};
    for (unsigned index = 0; index < 4; index++) {
        options.language = languages[index];
        assert(wm_channel_animation_pose(layout, "0001000148434745", WM_CHANNEL_ICON,
                                         120, &options));
        WmLayoutPaneState pane;
        assert(wm_layout_pane_state(layout, "txt_green", &pane));
        assert(strcmp(pane.text, texts[index]) == 0);
        assert(pane.font_size[0] == (float)(12 + (index == 3 ? 0 : index)));
        assert(pane.size[0] == 123 && pane.size[1] == 45);
    }
    assert(calls == 4);
    assert(wm_layout_pose(layout, NULL, 0));
    WmLayoutPaneState pane;
    assert(wm_layout_pane_state(layout, "txt_green", &pane));
    assert(!pane.text[0]);
    wm_layout_destroy(layout);
    assert(remove(companion) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    Fixture fixture;
    make_fixture(&fixture);
    WmChansLanguages result;
    char error[160];
    assert(wm_chans_languages_decode(fixture.bytes, sizeof(fixture.bytes), &result,
                                     error, sizeof(error)));
    assert(result.count == 3 && !error[0]);
    assert(strcmp(result.texts[1].language, "FRA") == 0);
    assert(result.texts[1].font_size[0] == 13);
    test_pose(&fixture, argv[1]);
    for (size_t size = 0; size < sizeof(fixture.bytes); size++)
        assert(!wm_chans_languages_decode(fixture.bytes, size, &result, error,
                                          sizeof(error)));
    static const size_t offsets[] = {0x30, 0x34, 0x40, 0x44, 0x48,
                                     0x4c, 0x50, 0x54, 0x60};
    for (size_t index = 0; index < sizeof(offsets) / sizeof(*offsets); index++) {
        Fixture invalid = fixture;
        put32(invalid.bytes, offsets[index], 0xffffffffu);
        expect_invalid(&invalid);
    }
    Fixture unicode = fixture;
    put16(unicode.bytes, 0x902, 0x00e9);
    assert(wm_chans_languages_decode(unicode.bytes, sizeof(unicode.bytes), &result,
                                     error, sizeof(error)));
    assert(memcmp(result.texts[0].text, "\xc3\xa9", 2) == 0);
    unicode = fixture;
    put16(unicode.bytes, 0x902, 0xd83c);
    put16(unicode.bytes, 0x904, 0xdf1f);
    assert(wm_chans_languages_decode(unicode.bytes, sizeof(unicode.bytes), &result,
                                     error, sizeof(error)));
    assert(memcmp(result.texts[0].text, "\xf0\x9f\x8c\x9f", 4) == 0);
    Fixture invalid = fixture;
    put16(invalid.bytes, 0x902, 0xd800); /* Unpaired UTF-16 surrogate. */
    expect_invalid(&invalid);
    invalid = fixture;
    invalid.bytes[fixture.setup] = 0xff;
    invalid.bytes[fixture.setup + 1] = 0xff; /* Bounded backward branch to itself. */
    expect_invalid(&invalid);
    invalid = fixture;
    invalid.bytes[fixture.title + 3] = 7; /* Unsupported TextWriter signature. */
    expect_invalid(&invalid);
    puts("CHANS language branches, source metrics, poses and malformed programs "
         "passed.");
    return 0;
}
