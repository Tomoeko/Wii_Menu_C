#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "texture_source.h"
#include "image_internal.h"
#include "wii_menu/render/image.h"

#include <fcntl.h>
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

static bool write_pixel(const char *path, const uint8_t pixels[4]) {
    WmImage image = { .width = 1, .height = 1, .pixels = (uint8_t *)pixels };
    return wm_image_write(path, &image);
}

static bool path_in_directory(char *path, size_t capacity,
                              const char *directory, const char *name) {
    int length = snprintf(path, capacity, "%s/%s", directory, name);
    return length >= 0 && (size_t)length < capacity;
}

static bool read_pixel(FILE *source, const uint8_t expected[4]) {
    size_t declared_bytes = 0;
    if (!wm_image_declared_bytes(source, &declared_bytes) ||
        declared_bytes != 4) {
        return false;
    }
    WmImage image;
    if (!wm_image_read_bounded_stream(source, declared_bytes, &image)) {
        return false;
    }
    bool matches = image.width == 1 && image.height == 1 &&
                   memcmp(image.pixels, expected, 4) == 0;
    wm_image_free(&image);
    return matches;
}

static int test_source_paths(void) {
    char directory[] = "/tmp/wm-texture-source-XXXXXX";
    char outside_directory[] = "/tmp/wm-texture-outside-XXXXXX";
    CHECK(mkdtemp(directory));
    CHECK(mkdtemp(outside_directory));

    char source_path[512];
    char moved_path[512];
    char alias_path[512];
    char directory_link[512];
    char outside_path[512];
    char nested_directory[512];
    char nested_path[512];
    char fifo_path[512];
    CHECK(path_in_directory(source_path, sizeof(source_path), directory,
                            "valid.wmra"));
    CHECK(path_in_directory(moved_path, sizeof(moved_path), directory,
                            "moved.wmra"));
    CHECK(path_in_directory(alias_path, sizeof(alias_path), directory,
                            "alias.wmra"));
    CHECK(path_in_directory(directory_link, sizeof(directory_link), directory,
                            "linked"));
    CHECK(path_in_directory(outside_path, sizeof(outside_path),
                            outside_directory, "child.wmra"));
    CHECK(path_in_directory(nested_directory, sizeof(nested_directory), directory,
                            "nested"));
    CHECK(path_in_directory(nested_path, sizeof(nested_path), nested_directory,
                            "valid.wmra"));
    CHECK(path_in_directory(fifo_path, sizeof(fifo_path), directory, "pipe.wmra"));

    const uint8_t original[4] = { 1, 2, 3, 4 };
    const uint8_t replacement[4] = { 5, 6, 7, 8 };
    CHECK(write_pixel(source_path, original));
    CHECK(write_pixel(outside_path, replacement));
    CHECK(mkdir(nested_directory, 0700) == 0);
    CHECK(write_pixel(nested_path, original));
    CHECK(mkfifo(fifo_path, 0600) == 0);
    int root_directory = open(directory, O_RDONLY | O_DIRECTORY);
    CHECK(root_directory >= 0);

    size_t url_length = strlen("valid.png");
    FILE *source = wm_texture_source_open(root_directory, "valid.png", url_length);
    CHECK(source);
    CHECK(read_pixel(source, original));
    CHECK(fclose(source) == 0);
    source = wm_texture_source_open(root_directory, "nested/valid.png",
                                    strlen("nested/valid.png"));
    CHECK(source);
    CHECK(read_pixel(source, original));
    CHECK(fclose(source) == 0);
    CHECK(!wm_texture_source_open(root_directory, "pipe.png",
                                  strlen("pipe.png")));

    CHECK(symlink("valid.wmra", alias_path) == 0);
    CHECK(!wm_texture_source_open(root_directory, "alias.png",
                                  strlen("alias.png")));
    CHECK(symlink(outside_directory, directory_link) == 0);
    CHECK(!wm_texture_source_open(root_directory, "linked/child.png",
                                  strlen("linked/child.png")));

    source = wm_texture_source_open(root_directory, "valid.png", url_length);
    CHECK(source);
    size_t declared_bytes = 0;
    CHECK(wm_image_declared_bytes(source, &declared_bytes));
    CHECK(declared_bytes == 4);
    CHECK(rename(source_path, moved_path) == 0);
    CHECK(symlink(outside_path, source_path) == 0);
    CHECK(!wm_texture_source_open(root_directory, "valid.png", url_length));

    WmImage loaded;
    CHECK(wm_image_read_bounded_stream(source, declared_bytes, &loaded));
    CHECK(memcmp(loaded.pixels, original, sizeof(original)) == 0);
    wm_image_free(&loaded);
    CHECK(fclose(source) == 0);

    CHECK(close(root_directory) == 0);
    CHECK(unlink(source_path) == 0);
    CHECK(unlink(moved_path) == 0);
    CHECK(unlink(alias_path) == 0);
    CHECK(unlink(directory_link) == 0);
    CHECK(unlink(fifo_path) == 0);
    CHECK(unlink(nested_path) == 0);
    CHECK(rmdir(nested_directory) == 0);
    CHECK(unlink(outside_path) == 0);
    CHECK(rmdir(directory) == 0);
    CHECK(rmdir(outside_directory) == 0);
    return 0;
}

static int test_root_replacement(void) {
    char directory[] = "/tmp/wm-texture-root-XXXXXX";
    CHECK(mkdtemp(directory));
    char old_path[512];
    char new_path[512];
    char moved_directory[512];
    CHECK(path_in_directory(old_path, sizeof(old_path), directory,
                            "valid.wmra"));
    int length = snprintf(moved_directory, sizeof(moved_directory),
                          "%s-moved", directory);
    CHECK(length >= 0 && (size_t)length < sizeof(moved_directory));
    CHECK(path_in_directory(new_path, sizeof(new_path), moved_directory,
                            "valid.wmra"));

    const uint8_t original[4] = { 1, 2, 3, 4 };
    const uint8_t replacement[4] = { 5, 6, 7, 8 };
    CHECK(write_pixel(old_path, original));
    int root_directory = open(directory, O_RDONLY | O_DIRECTORY);
    CHECK(root_directory >= 0);
    CHECK(rename(directory, moved_directory) == 0);
    CHECK(mkdir(directory, 0700) == 0);
    CHECK(write_pixel(old_path, replacement));

    FILE *source = wm_texture_source_open(root_directory, "valid.png",
                                           strlen("valid.png"));
    CHECK(source);
    CHECK(read_pixel(source, original));
    CHECK(fclose(source) == 0);

    CHECK(close(root_directory) == 0);
    CHECK(unlink(new_path) == 0);
    CHECK(unlink(old_path) == 0);
    CHECK(rmdir(moved_directory) == 0);
    CHECK(rmdir(directory) == 0);
    return 0;
}

int main(void) {
    size_t url_length = 0;
    CHECK(wm_texture_source_url_valid("valid.png", &url_length));
    CHECK(url_length == strlen("valid.png"));
    CHECK(!wm_texture_source_url_valid("../escape.png", &url_length));
    CHECK(!wm_texture_source_url_valid("/absolute.png", &url_length));
    CHECK(!wm_texture_source_url_valid("bad%20name.png", &url_length));
    CHECK(!wm_texture_source_url_valid("valid.png", NULL));
    CHECK(test_source_paths() == 0);
    CHECK(test_root_replacement() == 0);
    return 0;
}
