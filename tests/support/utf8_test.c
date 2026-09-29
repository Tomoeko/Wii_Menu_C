#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>

#include "wii_menu/support/utf8.h"

static void rejects(const uint8_t *bytes, size_t length) {
    assert(!wm_utf8_valid(bytes, length));
    size_t offset = 0;
    uint32_t codepoint = 123;
    size_t units = 7;
    assert(!wm_utf8_next(bytes, length, &offset, &codepoint, &units));
    assert(offset == 0 && codepoint == 123 && units == 7);
}

int main(void) {
    static const uint8_t valid[] = {'A',  0xC3, 0xA9, 0xE2, 0x82,
                                    0xAC, 0xF0, 0x9F, 0x98, 0x80};
    assert(wm_utf8_valid(valid, sizeof(valid)));
    assert(wm_utf8_cstr_valid("Menu"));
    assert(wm_utf8_valid(NULL, 0));
    assert(!wm_utf8_cstr_valid(NULL));

    size_t offset = 0;
    uint32_t codepoint = 0;
    size_t units = 0;
    assert(wm_utf8_next(valid, sizeof(valid), &offset, &codepoint, &units));
    assert(offset == 1 && codepoint == 'A' && units == 1);
    assert(wm_utf8_next(valid, sizeof(valid), &offset, &codepoint, &units));
    assert(offset == 3 && codepoint == 0xE9 && units == 1);
    assert(wm_utf8_next(valid, sizeof(valid), &offset, &codepoint, &units));
    assert(offset == 6 && codepoint == 0x20AC && units == 1);
    assert(wm_utf8_next(valid, sizeof(valid), &offset, &codepoint, &units));
    assert(offset == sizeof(valid) && codepoint == 0x1F600 && units == 2);

    static const uint8_t continuation[] = {0x80};
    static const uint8_t truncated[] = {0xE2, 0x82};
    static const uint8_t overlong[] = {0xE0, 0x80, 0x80};
    static const uint8_t surrogate[] = {0xED, 0xA0, 0x80};
    static const uint8_t too_high[] = {0xF4, 0x90, 0x80, 0x80};
    static const uint8_t bad_continuation[] = {0xC2, 'A'};
    rejects(continuation, sizeof(continuation));
    rejects(truncated, sizeof(truncated));
    rejects(overlong, sizeof(overlong));
    rejects(surrogate, sizeof(surrogate));
    rejects(too_high, sizeof(too_high));
    rejects(bad_continuation, sizeof(bad_continuation));
    return 0;
}
