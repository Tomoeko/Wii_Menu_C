#include "scene_assets.h"

#include <assert.h>
#include <string.h>

static void test_asset_paths(void) {
    char path[32];
    assert(wm_scene_asset_path(path, sizeof(path), "assets", "layouts/menu.json"));
    assert(strcmp(path, "assets/layouts/menu.json") == 0);

    assert(!wm_scene_asset_path(path, 0, "assets", "menu.json"));
    assert(!wm_scene_asset_path(path, 16, "assets", "layouts/menu.json"));
    assert(!wm_scene_asset_path(NULL, sizeof(path), "assets", "menu.json"));
    assert(!wm_scene_asset_path(path, sizeof(path), NULL, "menu.json"));
    assert(!wm_scene_asset_path(path, sizeof(path), "", "menu.json"));
    assert(!wm_scene_asset_path(path, sizeof(path), "assets", NULL));
    assert(!wm_scene_asset_path(path, sizeof(path), "assets", ""));

    static const char *const invalid[] = {"/menu.json",
                                          "../menu.json",
                                          "layouts/../menu.json",
                                          "layouts/./menu.json",
                                          "./menu.json",
                                          "layouts//menu.json",
                                          "layouts/",
                                          "layouts\\menu.json",
                                          "C:/menu.json",
                                          "menu.json:stream",
                                          "layouts/\nmenu.json"};
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); index++) {
        assert(!wm_scene_asset_path(path, sizeof(path), "assets", invalid[index]));
    }
}

static void test_layout_load(void) {
    WmLayout *layout =
        wm_scene_load_layout("tests/fixtures", "layout_fixture.json", NULL);
    assert(layout != NULL);
    wm_layout_destroy(layout);

    assert(wm_scene_load_layout("tests/fixtures", "../fixtures/layout_fixture.json",
                                NULL) == NULL);
}

int main(void) {
    test_asset_paths();
    test_layout_load();
    return 0;
}
