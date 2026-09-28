#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "export_internal.h"
#include "wii_menu/support/json.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition);     \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

const char *const wm_languages[10] = {"JPN", "ENG", "GER", "FRA", "SPA",
                                      "ITA", "NED", "CHN", "CHT", "KOR"};

/* The export command creates its output root before calling the writer. */
bool wm_output_parent(const char *path) {
    char parent[WM_PATH_CAP];
    size_t length = strlen(path);
    if (length >= sizeof(parent))
        return false;
    memcpy(parent, path, length + 1);
    char *separator = strrchr(parent, '/');
    if (!separator)
        return false;
    *separator = '\0';
    struct stat metadata;
    return lstat(parent, &metadata) == 0 && S_ISDIR(metadata.st_mode);
}

int main(void) {
    char directory[] = "/tmp/wm-channel-manifest-XXXXXX";
    CHECK(mkdtemp(directory));

    char sentinel[WM_PATH_CAP];
    char stale[WM_PATH_CAP];
    char manifest_path[WM_PATH_CAP];
    int length = snprintf(sentinel, sizeof(sentinel), "%s/sentinel", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(sentinel));
    length = snprintf(stale, sizeof(stale), "%s/channels.json.tmp", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(stale));
    length =
        snprintf(manifest_path, sizeof(manifest_path), "%s/channels.json", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(manifest_path));
    FILE *file = fopen(sentinel, "wb");
    CHECK(file);
    CHECK(fputs("untouched", file) >= 0);
    CHECK(fclose(file) == 0);
    CHECK(symlink(sentinel, stale) == 0);

    WmChannelList channels = {0};
    CHECK(wm_write_manifest(directory, "ENG", &channels, NULL));

    WmJson manifest = {0};
    CHECK(wm_json_load(&manifest, manifest_path, 65536));
    int schema = 0;
    CHECK(wm_json_integer(&manifest, wm_json_member(&manifest, 0, "schemaVersion"),
                          &schema));
    CHECK(schema == 1);
    CHECK(wm_json_equals(&manifest, wm_json_member(&manifest, 0, "language"), "ENG"));
    size_t catalog = wm_json_member(&manifest, 0, "channels");
    CHECK(catalog != WM_JSON_INVALID);
    CHECK(manifest.tokens[catalog].type == WM_JSON_ARRAY);
    CHECK(manifest.tokens[catalog].children == 0);
    wm_json_free(&manifest);

    file = fopen(sentinel, "rb");
    CHECK(file);
    char text[16] = {0};
    CHECK(fread(text, 1, 9, file) == 9);
    CHECK(strcmp(text, "untouched") == 0);
    CHECK(fclose(file) == 0);

    DIR *entries = opendir(directory);
    CHECK(entries);
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        CHECK(strncmp(entry->d_name, ".channels.json-", 15) != 0);
    }
    CHECK(closedir(entries) == 0);
    CHECK(unlink(manifest_path) == 0);
    CHECK(unlink(stale) == 0);
    CHECK(unlink(sentinel) == 0);
    CHECK(rmdir(directory) == 0);
    return 0;
}
