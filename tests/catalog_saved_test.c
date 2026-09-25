#define _POSIX_C_SOURCE 200809L

#include "wii_menu/menu.h"
#include "wii_menu/saved_layout.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int main(void) {
    char directory[128];
    int directory_length = snprintf(directory, sizeof(directory),
                                    "build/catalog-test-%ld", (long)getpid());
    assert(directory_length > 0 &&
           (size_t)directory_length < sizeof(directory));
    assert(mkdir(directory, 0700) == 0);

    char path[256];
    int length = snprintf(path, sizeof(path), "%s/channels.json", directory);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file);
    const char *catalog =
        "{\"schemaVersion\":1,\"channels\":["
        "{\"id\":\"0001000148414241\",\"title\":\"Photo\","
        "\"iconLayout\":\"../outside.json\"},"
        "{\"id\":\"other\",\"title\":\"News\","
        "\"iconLayout\":\"channel-layouts/other/icon/icon.json\"}],"
        "\"defaultOrder\":[\"other\",\"0001000148414241\"],"
        "\"savedLayout\":{\"slots\":[{\"page\":0,\"index\":1,"
        "\"id\":\"other\"}]}}";
    assert(fwrite(catalog, 1, strlen(catalog), file) == strlen(catalog));
    assert(fclose(file) == 0);

    uint8_t save[WM_SAVED_LAYOUT_BYTES] = {0};
    memcpy(save, "RIPL", 4);
    save[6] = 0x04;
    save[7] = 0xc0;
    save[11] = 3;
    save[15] = 2;
    save[0x10] = 1;
    save[0x20] = 3;
    const uint8_t title_id[8] = {0, 1, 0, 1, 0x48, 0x41, 0x42, 0x41};
    memcpy(save + 0x28, title_id, sizeof(title_id));
    const uint8_t checksum[16] = {
        0x38, 0x3a, 0x20, 0x9c, 0xe0, 0x73, 0xd3, 0x3b,
        0x07, 0x00, 0xd9, 0xbe, 0x1c, 0xa9, 0xc3, 0xe4
    };
    memcpy(save + sizeof(save) - sizeof(checksum), checksum,
           sizeof(checksum));
    length = snprintf(path, sizeof(path), "%s/iplsave.bin", directory);
    assert(length > 0 && (size_t)length < sizeof(path));
    file = fopen(path, "wb");
    assert(file);
    assert(fwrite(save, 1, sizeof(save), file) == sizeof(save));
    assert(fclose(file) == 0);

    WmMenu menu;
    wm_menu_init(&menu);
    assert(wm_catalog_load(&menu, directory));
    assert(strcmp(menu.slots[1].id, "0001000148414241") == 0);
    assert(menu.slots[1].icon_layout[0] == '\0');
    assert(strcmp(menu.slots[2].id, "other") == 0);
    assert(strcmp(menu.slots[2].icon_layout,
                  "channel-layouts/other/icon/icon.json") == 0);

    assert(unlink(path) == 0);
    length = snprintf(path, sizeof(path), "%s/channels.json", directory);
    assert(length > 0 && (size_t)length < sizeof(path));
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    return 0;
}
