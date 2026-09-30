#include "wii_menu/support/portable_path.h"

#include <assert.h>
#include <stdio.h>

static void reserved_components(void) {
    static const char *const reserved[] = {"CON",  "prn",      "aUx.txt", "NUL.bin",
                                           "com1", "COM9.dat", "lpt1",    "LPT9"};
    for (size_t index = 0; index < sizeof(reserved) / sizeof(reserved[0]); index++) {
        const char *name = reserved[index];
        assert(wm_path_reserved_component((const uint8_t *)name, strlen(name)));
    }
    static const char *const ordinary[] = {"",      "a",    "conifer", "com0",
                                           "com10", "lpt0", "lpt10",   "channel.bin"};
    for (size_t index = 0; index < sizeof(ordinary) / sizeof(ordinary[0]); index++) {
        const char *name = ordinary[index];
        assert(!wm_path_reserved_component((const uint8_t *)name, strlen(name)));
    }
    const uint8_t span[] = {'C', 'O', 'N', 'x'};
    assert(wm_path_reserved_component(span, 3));
    assert(!wm_path_reserved_component(span, 2));
    assert(!wm_path_reserved_component(span, sizeof(span)));
}

static void bounded_join(void) {
    const uint8_t component[] = {'a', 'b', 'c'};
    char *path = wm_path_join_component("dir", component, sizeof(component), 8);
    assert(path && strcmp(path, "dir/abc") == 0);
    free(path);
    assert(!wm_path_join_component("dir", component, sizeof(component), 7));
    assert(!wm_path_join_component("dir", component, sizeof(component), 4));
    assert(!wm_path_join_component("dir", component, sizeof(component), 0));
    assert(!wm_path_join_component("dir", component, SIZE_MAX, SIZE_MAX));

    path = wm_path_join_component("", component, sizeof(component), 4);
    assert(path && strcmp(path, "abc") == 0);
    free(path);
    assert(!wm_path_join_component("", component, sizeof(component), 3));
}

int main(void) {
    reserved_components();
    bounded_join();
    puts("Portable component names and bounded path joining passed.");
    return 0;
}
