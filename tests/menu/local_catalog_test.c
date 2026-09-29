#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#ifdef NDEBUG
#undef NDEBUG
#endif
#include "wii_menu/animation/channel_animation.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/menu/local_catalog.h"
#include "wii_menu/menu/menu.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_base(const char *directory) {
    char path[512];
    int length = snprintf(path, sizeof(path), "%s/channels.json", directory);
    assert(length > 0 && (size_t)length < sizeof(path));
    FILE *file = fopen(path, "wb");
    assert(file);
    const char *source =
        "{\"schemaVersion\":1,\"channels\":["
        "{\"id\":\"0001000248414341\",\"title\":\"Mii Channel\","
        "\"iconLayout\":\"channel-layouts/0001000248414341/icon/icon.json\","
        "\"bannerLayout\":\"channel-layouts/0001000248414341/banner/banner.json\"}],"
        "\"defaultOrder\":[\"0001000248414341\"]}";
    assert(fwrite(source, 1, strlen(source), file) == strlen(source));
    assert(fclose(file) == 0);
}

int main(void) {
    assert(wm_local_native_id_valid("0001000248414341"));
    assert(!wm_local_native_id_valid("000100024841434G"));
    assert(!wm_local_native_id_valid(NULL));

    char directory[] = "/tmp/wii-menu-local-catalog-XXXXXX";
    assert(mkdtemp(directory));
    write_base(directory);

    WmLocalCatalog local = {0};
    strcpy(local.channels[0].id, "custom-example");
    strcpy(local.channels[0].title, "Example Channel");
    local.channel_count = 1;
    strcpy(local.hidden[0], "0001000248414341");
    local.hidden_count = 1;
    assert(wm_local_catalog_save(directory, &local));

    WmMenu menu;
    wm_menu_init(&menu);
    assert(wm_catalog_load(&menu, directory));
    assert(strcmp(menu.slots[1].id, "custom-example") == 0);
    assert(strcmp(menu.slots[1].icon_layout,
                  "custom-channels/custom-example/icon.json") == 0);
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        assert(strcmp(menu.slots[slot].id, "0001000248414341") != 0);
    }

    local.channels[1] = (WmLocalChannel){0};
    strcpy(local.channels[1].id, "0001000148414445");
    strcpy(local.channels[1].title, "Internet Channel");
    local.channels[1].imported = true;
    local.channel_count = 2;
    assert(wm_local_catalog_save(directory, &local));
    wm_menu_init(&menu);
    assert(wm_catalog_load(&menu, directory));
    assert(strcmp(menu.slots[2].id, "0001000148414445") == 0);
    assert(strcmp(menu.slots[2].banner_layout,
                  "channel-layouts/0001000148414445/banner/banner.json") == 0);

    /* Moving an installed channel to a sparse later page must not cause
     * preview arrows to stop at the empty slots in between. */
    assert(wm_menu_move_channel(&menu, 2, 27));
    assert(wm_menu_select(&menu, 1));
    wm_menu_tick(&menu, 28.0f / 60.0f);
    assert(wm_menu_change_preview(&menu, 1));
    assert(menu.selected == 27 && menu.page == 2);
    wm_menu_tick(&menu, 20.0f / 60.0f);
    assert(wm_menu_change_preview(&menu, -1));
    assert(menu.selected == 1 && menu.page == 0);
    wm_menu_tick(&menu, 20.0f / 60.0f);
    assert(wm_menu_change_preview(&menu, -1));
    assert(menu.selected == 0 && menu.page == 0);
    wm_menu_tick(&menu, 20.0f / 60.0f);
    assert(wm_menu_change_preview(&menu, -1));
    assert(menu.selected == 27 && menu.page == 2);

    WmLayout *layout = wm_layout_load_json(
        "examples/custom-channels/custom-example/icon.json", NULL, 0);
    assert(layout);
    WmChannelAnimationOptions options = {0};
    assert(wm_channel_animation_pose(layout, "custom-example", WM_CHANNEL_ICON, 0.0f,
                                     &options));
    wm_layout_destroy(layout);

    WmLocalCatalog loaded;
    assert(wm_local_catalog_load(directory, &loaded));
    assert(loaded.channel_count == 2 && loaded.hidden_count == 1);
    assert(loaded.channels[1].imported);

    char path[512];
    snprintf(path, sizeof(path), "%s/channels.local.json", directory);
    assert(unlink(path) == 0);
    snprintf(path, sizeof(path), "%s/channels.json", directory);
    assert(unlink(path) == 0);
    assert(rmdir(directory) == 0);
    return 0;
}
