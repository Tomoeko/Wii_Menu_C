#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "texture_source.h"
#include "wii_menu/render/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition);     \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

int main(void) {
    size_t url_length = 0;
    CHECK(wm_texture_source_url_valid("valid.png", &url_length));
    CHECK(url_length == strlen("valid.png"));
    CHECK(!wm_texture_source_url_valid("../escape.png", &url_length));
    CHECK(!wm_texture_source_url_valid("/absolute.png", &url_length));
    CHECK(!wm_texture_source_url_valid("bad%20name.png", &url_length));

    char directory[] = "/tmp/wm-texture-source-XXXXXX";
    CHECK(mkdtemp(directory));
    char path[256];
    int length = snprintf(path, sizeof(path), "%s/valid.wmra", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(path));
    uint8_t pixels[4] = {1, 2, 3, 4};
    WmImage image = {.width = 1, .height = 1, .pixels = pixels};
    CHECK(wm_image_write(path, &image));

    char *canonical_root = realpath(directory, NULL);
    CHECK(canonical_root);
    char *resolved = wm_texture_source_resolve(canonical_root, "valid.png", url_length);
    CHECK(resolved);
    size_t declared_bytes = 0;
    CHECK(wm_texture_source_declared_bytes(resolved, &declared_bytes));
    CHECK(declared_bytes == sizeof(pixels));
    free(resolved);

    char outside[] = "/tmp/wm-texture-outside-XXXXXX";
    int descriptor = mkstemp(outside);
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    char link_path[256];
    length = snprintf(link_path, sizeof(link_path), "%s/escape.wmra", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(link_path));
    CHECK(symlink(outside, link_path) == 0);
    CHECK(wm_texture_source_url_valid("escape.png", &url_length));
    CHECK(!wm_texture_source_resolve(canonical_root, "escape.png", url_length));
    free(canonical_root);

    CHECK(unlink(link_path) == 0);
    CHECK(unlink(outside) == 0);
    CHECK(unlink(path) == 0);
    CHECK(rmdir(directory) == 0);
    return 0;
}
