#include "wii_menu/resources/wm_pack.h"

#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
    if (argc != 4 || (strcmp(argv[1], "pack") != 0 &&
                      strcmp(argv[1], "unpack") != 0)) {
        fputs("Usage: wm-pack pack ASSET_DIRECTORY FILE.wm\n"
              "       wm-pack unpack FILE.wm NEW_DIRECTORY\n", stderr);
        return argc == 2 && strcmp(argv[1], "--help") == 0 ? 0 : 2;
    }
    char error[256];
    bool okay = strcmp(argv[1], "pack") == 0 ?
        wm_pack_create(argv[2], argv[3], error, sizeof(error)) :
        wm_pack_extract(argv[2], argv[3], error, sizeof(error));
    if (!okay) fprintf(stderr, "%s\n", error);
    return okay ? 0 : 1;
}
