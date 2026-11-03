#include "console_common/support/host.h"
#include "wii_menu/persistence/channel_layout_store.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void default_menu(WmMenu *menu) {
    wm_menu_init(menu);
    menu->slots[1] = (WmChannel){.id = "first", .title = "First", .occupied = true};
    menu->slots[2] = (WmChannel){.id = "second", .title = "Second", .occupied = true};
    menu->slots[14] = (WmChannel){.id = "custom-\"slash\\tab\t",
                                  .title = "Escaped ID",
                                  .icon_layout = "custom-channels/example/icon.json",
                                  .occupied = true};
}

static void write_text(const char *path, const char *text) {
    FILE *file = cc_host_fopen(path, "wb");
    assert(file);
    assert(fwrite(text, 1, strlen(text), file) == strlen(text));
    assert(fclose(file) == 0);
}

static char *read_text(const char *path) {
    FILE *file = cc_host_fopen(path, "rb");
    assert(file);
    assert(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file);
    assert(length >= 0 && fseek(file, 0, SEEK_SET) == 0);
    char *contents = malloc((size_t)length + 1);
    assert(contents);
    assert(fread(contents, 1, (size_t)length, file) == (size_t)length);
    contents[length] = '\0';
    assert(fclose(file) == 0);
    return contents;
}

static void assert_missing(const char *path) {
    errno = 0;
    FILE *file = cc_host_fopen(path, "rb");
    assert(!file && errno == ENOENT);
}

static void write_entries(const char *path, const char *entries) {
    FILE *file = cc_host_fopen(path, "wb");
    assert(file);
    assert(fprintf(file,
                   "{\n    \"schemaVersion\": 1,\n    \"slots\": [\n%s\n    ]\n}\n",
                   entries) > 0);
    assert(fclose(file) == 0);
}

static void test_round_trip(const char *path) {
    WmMenu menu;
    default_menu(&menu);
    WmMenu defaults = menu;
    char error[160];
    WmChannelLayoutStatus status;
    WmChannelLayoutStore *store =
        wm_channel_layout_store_open(path, &menu, &status, error, sizeof(error));
    assert(store && status == WM_CHANNEL_LAYOUT_MISSING && !error[0]);
    assert_missing(path);
    assert(wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    assert_missing(path);
    assert(!wm_menu_move_channel(&menu, 0, 3));
    assert(!wm_menu_move_channel(&menu, 1, 2));
    assert(wm_menu_move_channel(&menu, 1, 47));
    assert(wm_menu_move_channel(&menu, 47, 1));
    assert(wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    assert_missing(path);

    assert(wm_menu_move_channel(&menu, 1, 47));
    assert(wm_menu_move_channel(&menu, 14, 25));
    assert(wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    char *saved = read_text(path);
    assert(strstr(saved, "\n    \"schemaVersion\": 1,\n"));
    assert(strstr(saved, "custom-\\\"slash\\\\tab\\u0009"));

    /* External edits make redundant writes observable without clock precision
     * or inode assumptions. No position change must not touch this file. */
    write_text(path, "externally edited while running\n");
    strcpy(menu.slots[47].title, "Changed metadata");
    menu.page = 3;
    assert(wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    char *untouched = read_text(path);
    assert(strcmp(untouched, "externally edited while running\n") == 0);
    free(untouched);
    assert(wm_menu_move_channel(&menu, 47, 46));
    assert(!wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    assert(strstr(error, "changed externally"));
    wm_channel_layout_store_destroy(store);

    write_text(path, saved);
    free(saved);
    default_menu(&menu);
    store = wm_channel_layout_store_open(path, &menu, &status, error, sizeof(error));
    assert(store && status == WM_CHANNEL_LAYOUT_LOADED && !error[0]);
    assert(!menu.slots[1].occupied && !menu.slots[14].occupied);
    assert(memcmp(&menu.slots[47], &defaults.slots[1], sizeof(WmChannel)) == 0);
    assert(memcmp(&menu.slots[25], &defaults.slots[14], sizeof(WmChannel)) == 0);
    assert(wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    wm_channel_layout_store_destroy(store);
    assert(remove(path) == 0);
}

static void test_manual_swap(const char *path) {
    write_entries(path, "        {\n"
                        "            \"id\": \"first\",\n"
                        "            \"page\": 0,\n"
                        "            \"index\": 2\n"
                        "        },\n"
                        "        {\n"
                        "            \"id\": \"second\",\n"
                        "            \"page\": 0,\n"
                        "            \"index\": 1\n"
                        "        }");
    WmMenu menu;
    default_menu(&menu);
    char error[160];
    WmChannelLayoutStatus status;
    WmChannelLayoutStore *store =
        wm_channel_layout_store_open(path, &menu, &status, error, sizeof(error));
    assert(store && status == WM_CHANNEL_LAYOUT_LOADED);
    assert(strcmp(menu.slots[1].id, "second") == 0);
    assert(strcmp(menu.slots[2].id, "first") == 0);
    assert(menu.slots[14].occupied);
    wm_channel_layout_store_destroy(store);
    assert(remove(path) == 0);
}

static void assert_defaults(const char *path, const char *reason) {
    WmMenu menu;
    default_menu(&menu);
    WmMenu defaults = menu;
    char *before = read_text(path);
    char error[160];
    WmChannelLayoutStatus status;
    WmChannelLayoutStore *store =
        wm_channel_layout_store_open(path, &menu, &status, error, sizeof(error));
    assert(store && status == WM_CHANNEL_LAYOUT_INVALID);
    assert(strstr(error, reason));
    assert(memcmp(&menu, &defaults, sizeof(menu)) == 0);
    assert(wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    char *after = read_text(path);
    assert(strcmp(before, after) == 0);
    free(before);
    free(after);
    wm_channel_layout_store_destroy(store);
}

static void test_invalid_entries(const char *path) {
    static const struct {
        const char *entries;
        const char *reason;
    } cases[] = {
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 1,\n"
         "            \"index\": 0\n"
         "        },\n"
         "        {\n"
         "            \"id\": \"second\",\n"
         "            \"page\": 1,\n"
         "            \"index\": 0\n"
         "        }",
         "Overlapped"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 0,\n"
         "            \"index\": 2\n"
         "        }",
         "Overlapped"},
        {"        {\n"
         "            \"id\": \"disc\",\n"
         "            \"page\": 3,\n"
         "            \"index\": 11\n"
         "        }",
         "Disc Channel"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 0,\n"
         "            \"index\": 0\n"
         "        }",
         "Disc Channel"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 1,\n"
         "            \"index\": 0\n"
         "        },\n"
         "        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 2,\n"
         "            \"index\": 0\n"
         "        }",
         "Repeated"},
        {"        {\n"
         "            \"id\": \"removed-channel\",\n"
         "            \"page\": 0,\n"
         "            \"index\": 3\n"
         "        }",
         "Unknown"},
        {"        {\n"
         "            \"id\": \"first\\u0000suffix\",\n"
         "            \"page\": 0,\n"
         "            \"index\": 3\n"
         "        }",
         "entry"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 4,\n"
         "            \"index\": 0\n"
         "        }",
         "entry"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": -1,\n"
         "            \"index\": 0\n"
         "        }",
         "entry"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 0,\n"
         "            \"index\": 12\n"
         "        }",
         "entry"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 0,\n"
         "            \"index\": 1.5\n"
         "        }",
         "entry"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"page\": 0,\n"
         "            \"index\": 99999999999999999999\n"
         "        }",
         "entry"},
        {"        {\n"
         "            \"id\": \"first\",\n"
         "            \"index\": 1,\n"
         "            \"index\": 2\n"
         "        }",
         "entry"},
    };
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        write_entries(path, cases[index].entries);
        assert_defaults(path, cases[index].reason);
    }
}

static void test_invalid_configs(const char *path) {
    write_text(path, "{\n    \"schemaVersion\": 2,\n    \"slots\": []\n}\n");
    assert_defaults(path, "schema");
    write_text(path, "{\n    \"schemaVersion\": 1,\n    \"slots\": {}\n}\n");
    assert_defaults(path, "schema");
    write_text(path, "{\n");
    assert_defaults(path, "Malformed");
    write_text(path, "");
    assert_defaults(path, "Malformed");

    /* A genuine move can replace an invalid config with a complete valid one. */
    WmMenu menu;
    default_menu(&menu);
    char error[160];
    WmChannelLayoutStatus status;
    WmChannelLayoutStore *store =
        wm_channel_layout_store_open(path, &menu, &status, error, sizeof(error));
    assert(store && status == WM_CHANNEL_LAYOUT_INVALID);
    assert(wm_menu_move_channel(&menu, 2, 36));
    assert(wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    wm_channel_layout_store_destroy(store);
    default_menu(&menu);
    store = wm_channel_layout_store_open(path, &menu, &status, error, sizeof(error));
    assert(store && status == WM_CHANNEL_LAYOUT_LOADED);
    assert(strcmp(menu.slots[36].id, "second") == 0);
    wm_channel_layout_store_destroy(store);
    assert(remove(path) == 0);
}

static void test_io_failures(const char *path) {
    WmMenu menu;
    default_menu(&menu);
    WmMenu defaults = menu;
    char error[160];
    WmChannelLayoutStatus status;
    char missing_parent[4096];
    int length =
        snprintf(missing_parent, sizeof(missing_parent), "%s/config.json", path);
    assert(length > 0 && (size_t)length < sizeof(missing_parent));
    WmChannelLayoutStore *store = wm_channel_layout_store_open(
        missing_parent, &menu, &status, error, sizeof(error));
    assert(store && status == WM_CHANNEL_LAYOUT_MISSING);
    assert(wm_menu_move_channel(&menu, 1, 47));
    assert(!wm_channel_layout_store_save(store, &menu, error, sizeof(error)));
    assert(strstr(error, "Could not save"));
    assert_missing(path);
    wm_channel_layout_store_destroy(store);

    FILE *file = cc_host_fopen(path, "wb");
    assert(file);
    for (size_t index = 0; index <= 64 * 1024; index++)
        assert(fputc(' ', file) != EOF);
    assert(fclose(file) == 0);
    default_menu(&menu);
    store = wm_channel_layout_store_open(path, &menu, &status, error, sizeof(error));
    assert(!store && status == WM_CHANNEL_LAYOUT_ERROR && error[0]);
    assert(memcmp(&menu, &defaults, sizeof(menu)) == 0);
    assert(remove(path) == 0);
}

int main(int argc, char **argv) {
    assert(argc == 2);
    assert_missing(argv[1]);
    test_round_trip(argv[1]);
    test_manual_swap(argv[1]);
    test_invalid_entries(argv[1]);
    test_invalid_configs(argv[1]);
    test_io_failures(argv[1]);
    assert_missing(argv[1]);
    return 0;
}
