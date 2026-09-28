#include "board_text.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void expect_character(const char *text, size_t length, size_t expected_bytes,
                             size_t expected_units) {
    size_t bytes = 0;
    size_t units = 0;
    assert(wm_board_text_character(text, length, &bytes, &units));
    assert(bytes == expected_bytes);
    assert(units == expected_units);
}

static void expect_invalid_character(const char *text, size_t length) {
    size_t bytes = 0;
    size_t units = 0;
    assert(!wm_board_text_character(text, length, &bytes, &units));
}

static void test_utf8(void) {
    expect_character("A", 1, 1, 1);
    expect_character("\xc3\xa9", 2, 2, 1);
    expect_character("\xe2\x82\xac", 3, 3, 1);
    expect_character("\xf0\x9f\x98\x80", 4, 4, 2);
    expect_invalid_character("\xc0\x80", 2);
    expect_invalid_character("\xe0\x80\x80", 3);
    expect_invalid_character("\xed\xa0\x80", 3);
    expect_invalid_character("\xf4\x90\x80\x80", 4);
    expect_invalid_character("\xc3", 1);
    expect_invalid_character("\x80", 1);

    const char *valid = "A\xc3\xa9\xf0\x9f\x98\x80";
    assert(wm_board_text_utf16_units(valid, strlen(valid)) == 4);
    assert(wm_board_text_utf16_units("\xc0\x80", 2) == SIZE_MAX);
}

static void test_memo_input(void) {
    size_t bytes = 17;
    bool whitespace = true;
    assert(wm_board_text_memo_input("A\xc3\xa9\nB", 7, &bytes, &whitespace));
    assert(bytes == 5);
    assert(whitespace);
    assert(wm_board_text_memo_input("\xf0\x9f\x98\x80", 4, &bytes, &whitespace));
    assert(bytes == 4);
    assert(!whitespace);
    assert(!wm_board_text_memo_input("", 4, &bytes, &whitespace));
    assert(!wm_board_text_memo_input("ABCD!", 4, &bytes, &whitespace));
    assert(!wm_board_text_memo_input("A\tB", 4, &bytes, &whitespace));
    assert(!wm_board_text_memo_input("\xed\xa0\x80", 4, &bytes, &whitespace));
    /* Failed scans do not publish partial metadata. */
    assert(bytes == 4);
    assert(!whitespace);
}

static void test_bounded_edits(void) {
    char draft[16] = "A\xc3\xa9Z";
    size_t bytes = strlen(draft);
    size_t caret = 1;
    caret = 2; /* A continuation byte cannot be an insertion point. */
    assert(!wm_board_text_insert(draft, sizeof(draft), &bytes, &caret, "x", 1));
    assert(strcmp(draft, "A\xc3\xa9Z") == 0 && bytes == 4 && caret == 2);
    caret = 3;
    assert(!wm_board_text_replace_before_caret(draft, sizeof(draft), &bytes, &caret, 1,
                                               "x", 1));
    assert(strcmp(draft, "A\xc3\xa9Z") == 0 && bytes == 4 && caret == 3);
    caret = 1;
    assert(wm_board_text_insert(draft, sizeof(draft), &bytes, &caret,
                                "\xf0\x9f\x98\x80", 4));
    assert(strcmp(draft, "A\xf0\x9f\x98\x80\xc3\xa9Z") == 0);
    assert(bytes == 8 && caret == 5);

    assert(wm_board_text_backspace(draft, sizeof(draft), &bytes, &caret));
    assert(strcmp(draft, "A\xc3\xa9Z") == 0);
    assert(bytes == 4 && caret == 1);

    caret = 3;
    assert(wm_board_text_replace_before_caret(draft, sizeof(draft), &bytes, &caret, 2,
                                              "hello", 5));
    assert(strcmp(draft, "AhelloZ") == 0);
    assert(bytes == 7 && caret == 6);

    assert(
        !wm_board_text_insert(draft, sizeof(draft), &bytes, &caret, "0123456789", 10));
    assert(!wm_board_text_replace_before_caret(draft, sizeof(draft), &bytes, &caret, 1,
                                               "0123456789", 10));
    assert(strcmp(draft, "AhelloZ") == 0);
    assert(bytes == 7 && caret == 6);

    caret = 8;
    assert(!wm_board_text_backspace(draft, sizeof(draft), &bytes, &caret));
    assert(strcmp(draft, "AhelloZ") == 0);
    assert(bytes == 7 && caret == 8);
}

int main(void) {
    test_utf8();
    test_memo_input();
    test_bounded_edits();
    puts("Board text validation and edits passed.");
    return 0;
}
