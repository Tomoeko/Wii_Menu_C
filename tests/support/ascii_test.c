#include "wii_menu/support/ascii.h"

#include <assert.h>
#include <stdio.h>

int main(void) {
    assert(wm_ascii_lower('A') == 'a');
    assert(wm_ascii_lower('z') == 'z');
    assert(wm_ascii_lower(0xc3) == 0xc3);
    assert(wm_ascii_equal_ignore_case("", ""));
    assert(wm_ascii_equal_ignore_case("Folder/File", "folder/file"));
    assert(!wm_ascii_equal_ignore_case("Folder", "Folder/File"));
    assert(!wm_ascii_equal_ignore_case("", "a"));
    assert(wm_ascii_equal_ignore_case("Caf\xc3\xa9", "caf\xc3\xa9"));
    assert(!wm_ascii_equal_ignore_case("\xc3\x89", "\xc3\xa9"));
    assert(wm_ascii_hash_ignore_case("HELLO") == UINT64_C(0xa430d84680aabd0b));
    assert(wm_ascii_hash_ignore_case("Folder/File") ==
           wm_ascii_hash_ignore_case("folder/file"));
    puts("ASCII case comparison and hashing passed.");
    return 0;
}
