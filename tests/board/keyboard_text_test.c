#include "keyboard_text.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void expect_codepoint(const char *text, size_t length, uint32_t expected,
                             size_t consumed) {
    const char *cursor = text;
    assert(wm_keyboard_text_next_codepoint(&cursor, text + length) == expected);
    assert((size_t)(cursor - text) == consumed);
}

int main(void) {
    expect_codepoint("A", 1, 'A', 1);
    expect_codepoint("\xc3\xa9", 2, 0xe9u, 2);
    expect_codepoint("\xe2\x82\xac", 3, 0x20acu, 3);
    expect_codepoint("\xf0\x9f\x98\x80", 4, 0x1f600u, 4);

    /* Each malformed sequence advances, but cannot become a learned word. */
    expect_codepoint("\xc0\x80", 2, 0xfffdu, 1);
    expect_codepoint("\xe0\x80\x80", 3, 0xfffdu, 1);
    expect_codepoint("\xed\xa0\x80", 3, 0xfffdu, 1);
    expect_codepoint("\xf4\x90\x80\x80", 4, 0xfffdu, 1);
    expect_codepoint("\xc3", 1, 0xfffdu, 1);
    expect_codepoint("\x80", 1, 0xfffdu, 1);
    assert(!wm_keyboard_text_is_word_point(0xfffdu));

    assert(
        wm_keyboard_text_prefix_matches("École", strlen("École"), "éc", strlen("éc")));
    assert(
        !wm_keyboard_text_prefix_matches("café", strlen("café"), "cat", strlen("cat")));

    bool exact = false;
    assert(wm_keyboard_text_phone_digits_match("café", strlen("café"), "2233", &exact));
    assert(exact);
    assert(wm_keyboard_text_phone_digits_match("café", strlen("café"), "223", &exact));
    assert(!exact);
    const char alphabet[] = "abcdefghijklmnopqrstuvwxyz";
    assert(wm_keyboard_text_phone_digits_match(alphabet, strlen(alphabet),
                                               "22233344455566677778889999", &exact));
    assert(exact);
    assert(
        !wm_keyboard_text_phone_digits_match("café", strlen("café"), "9999", &exact));
    assert(!exact);

    char candidate[16];
    assert(wm_keyboard_text_copy_candidate_case(candidate, sizeof(candidate), "école",
                                                strlen("école"), "Éc", strlen("Éc"),
                                                false, false));
    assert(strcmp(candidate, "École") == 0);
    assert(!wm_keyboard_text_copy_candidate_case(candidate, 1, "école", strlen("école"),
                                                 "Éc", strlen("Éc"), false, false));

    puts("Keyboard UTF-8 and prediction text passed.");
    return 0;
}
