#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "wii_menu/render/image.h"
#include "image_internal.h"

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

int main(void) {
    char directory[] = "/tmp/wm-image-atomic-XXXXXX";
    CHECK(mkdtemp(directory));

    char sentinel[256];
    char destination[256];
    int length = snprintf(sentinel, sizeof(sentinel), "%s/sentinel", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(sentinel));
    length = snprintf(destination, sizeof(destination), "%s/image.wmra", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(destination));

    FILE *file = fopen(sentinel, "wb");
    CHECK(file);
    CHECK(fwrite("guard", 1, 5, file) == 5);
    CHECK(fclose(file) == 0);
    CHECK(symlink(sentinel, destination) == 0);

    uint8_t pixels[4] = {11, 22, 33, 44};
    WmImage image = {.width = 1, .height = 1, .pixels = pixels};
    CHECK(wm_image_write(destination, &image));

    struct stat metadata;
    CHECK(lstat(destination, &metadata) == 0 && S_ISREG(metadata.st_mode));
    WmImage bounded = {0};
    CHECK(!wm_image_read_bounded(destination, 3, &bounded));
    CHECK(bounded.pixels == NULL);
    WmImage loaded = {0};
    CHECK(wm_image_read(destination, &loaded));
    CHECK(loaded.width == 1 && loaded.height == 1);
    CHECK(memcmp(loaded.pixels, pixels, sizeof(pixels)) == 0);
    wm_image_free(&loaded);

    file = fopen(sentinel, "rb");
    CHECK(file);
    char contents[6] = {0};
    CHECK(fread(contents, 1, 5, file) == 5);
    CHECK(strcmp(contents, "guard") == 0);
    CHECK(fclose(file) == 0);

    DIR *entries = opendir(directory);
    CHECK(entries);
    struct dirent *entry;
    while ((entry = readdir(entries)) != NULL) {
        CHECK(strstr(entry->d_name, ".tmp.") == NULL);
    }
    CHECK(closedir(entries) == 0);
    CHECK(unlink(destination) == 0);
    CHECK(unlink(sentinel) == 0);
    CHECK(rmdir(directory) == 0);
    return 0;
}
