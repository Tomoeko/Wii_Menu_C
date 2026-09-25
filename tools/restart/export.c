#include "wii_menu/image.h"
#include "wii_menu/resource_layout.h"
#include "wii_menu/resource_tpl.h"
#include "wii_menu/resource_u8.h"

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { PATH_CAPACITY = 4096, MAX_EXECUTABLE_SIZE = 32 * 1024 * 1024 };

static const char *const layout_member =
    "arc/blyt/my_BackToWiiMenu.brlyt";
static const char *const animation_member =
    "arc/anim/my_BackToWiiMenu.brlan";
static const char *const texture_members[] = {
    "arc/timg/IplTopMask4x3.tpl",
    "arc/timg/my_WiiLogoWait.tpl"
};

static uint32_t read_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static char *copy_string(const char *value) {
    size_t length = strlen(value) + 1;
    char *copy = malloc(length);
    if (copy) memcpy(copy, value, length);
    return copy;
}

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || length > MAX_EXECUTABLE_SIZE ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *data = malloc((size_t)length ? (size_t)length : 1);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static bool archive_extent(const uint8_t *data, size_t available,
                           size_t *extent) {
    if (available < 0x20 || read_be32(data) != UINT32_C(0x55aa382d))
        return false;
    size_t root = read_be32(data + 4);
    if (root > available - 12) return false;
    size_t count = read_be32(data + root + 8);
    if (count == 0 || count > (available - root) / 12) return false;
    size_t end = root + count * 12;
    for (size_t index = 1; index < count; index++) {
        const uint8_t *node = data + root + index * 12;
        if (node[0] != 0) continue;
        size_t start = read_be32(node + 4);
        size_t length = read_be32(node + 8);
        if (start > available || length > available - start) return false;
        if (start + length > end) end = start + length;
    }
    *extent = end;
    return true;
}

static bool in_executable_section(const uint8_t *data, size_t size,
                                  size_t offset, size_t length) {
    if (size < 0x100 || offset > size || length > size - offset) return false;
    for (size_t index = 0; index < 18; index++) {
        size_t start = read_be32(data + index * 4);
        size_t section_size = read_be32(data + 0x90 + index * 4);
        if (start >= 0x100 && start <= offset && section_size <= size - start &&
            offset - start <= section_size &&
            length <= section_size - (offset - start)) return true;
    }
    return false;
}

static bool find_archive(const uint8_t *data, size_t size,
                         WmU8Archive *archive) {
    for (size_t offset = 0x100; offset + 4 <= size; offset++) {
        if (read_be32(data + offset) != UINT32_C(0x55aa382d)) continue;
        size_t extent = 0;
        if (!archive_extent(data + offset, size - offset, &extent) ||
            !in_executable_section(data, size, offset, extent)) continue;
        char error[160] = {0};
        if (!wm_u8_parse(data + offset, extent, archive, error,
                         sizeof(error))) continue;
        bool found = wm_u8_find(archive, layout_member) &&
                     wm_u8_find(archive, animation_member);
        for (size_t index = 0;
             index < sizeof(texture_members) / sizeof(texture_members[0]);
             index++) found &= wm_u8_find(archive, texture_members[index]) != NULL;
        if (found) return true;
        wm_u8_free(archive);
    }
    return false;
}

static bool ensure_directory(const char *path) {
    if (mkdir(path, 0755) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat status;
    return stat(path, &status) == 0 && S_ISDIR(status.st_mode);
}

static bool subdirectory(const char *output, const char *name) {
    char path[PATH_CAPACITY];
    int length = snprintf(path, sizeof(path), "%s/%s", output, name);
    return length > 0 && length < (int)sizeof(path) && ensure_directory(path);
}

static bool export_texture(const WmU8Entry *member, const char *output,
                           WmResourceTexture *descriptor) {
    char error[160] = {0};
    WmTpl tpl = {0};
    if (!wm_tpl_decode(member->data, member->size, &tpl, error,
                       sizeof(error)) || tpl.count != 1) {
        fprintf(stderr, "Could not decode embedded restart texture: %s\n", error);
        wm_tpl_free(&tpl);
        return false;
    }
    const char *name = strrchr(member->path, '/');
    name = name ? name + 1 : member->path;
    size_t stem_length = strlen(name) - 4;
    char stem[80];
    if (stem_length == 0 || stem_length >= sizeof(stem)) {
        wm_tpl_free(&tpl);
        return false;
    }
    memcpy(stem, name, stem_length);
    stem[stem_length] = '\0';
    char path[PATH_CAPACITY];
    char url[160];
    char source[160];
    int path_length = snprintf(path, sizeof(path), "%s/textures/restart/%s.wmra",
                               output, stem);
    int url_length = snprintf(url, sizeof(url),
                              "textures/restart/%s.png", stem);
    int source_length = snprintf(source, sizeof(source),
                                 "system-menu-executable/embedded-restart/%s",
                                 member->path);
    WmImage image = {tpl.images[0].width, tpl.images[0].height,
                     tpl.images[0].rgba};
    bool valid = path_length > 0 && path_length < (int)sizeof(path) &&
                 url_length > 0 && url_length < (int)sizeof(url) &&
                 source_length > 0 && source_length < (int)sizeof(source) &&
                 wm_image_write(path, &image);
    if (valid) {
        char *owned_name = copy_string(name);
        char *owned_url = copy_string(url);
        char *owned_source = copy_string(source);
        valid = owned_name && owned_url && owned_source;
        if (valid) {
            *descriptor = (WmResourceTexture){
                owned_name, owned_url, image.width, image.height,
                tpl.images[0].format, owned_source
            };
        } else {
            free(owned_name);
            free(owned_url);
            free(owned_source);
        }
    }
    wm_tpl_free(&tpl);
    return valid;
}

static bool write_json(const char *output, const char *json, size_t size) {
    char path[PATH_CAPACITY];
    int length = snprintf(path, sizeof(path),
                          "%s/layouts/restart/my_BackToWiiMenu.json", output);
    if (length <= 0 || length >= (int)sizeof(path)) return false;
    FILE *file = fopen(path, "wb");
    if (!file) return false;
    bool valid = fwrite(json, 1, size, file) == size;
    if (fclose(file) != 0) valid = false;
    if (!valid) remove(path);
    return valid;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: wm-restart-export EXECUTABLE_APP LOCAL_OUTPUT_DIRECTORY\n");
        return 2;
    }
    size_t size = 0;
    uint8_t *data = read_file(argv[1], &size);
    if (!data) {
        fprintf(stderr, "Could not read System Menu executable content.\n");
        return 1;
    }
    WmU8Archive archive = {0};
    if (!find_archive(data, size, &archive)) {
        fprintf(stderr, "The executable has no supported embedded restart archive.\n");
        free(data);
        return 1;
    }
    bool valid = ensure_directory(argv[2]) &&
                 subdirectory(argv[2], "layouts") &&
                 subdirectory(argv[2], "textures") &&
                 subdirectory(argv[2], "layouts/restart") &&
                 subdirectory(argv[2], "textures/restart");
    WmResourceTexture textures[2] = {0};
    for (size_t index = 0; index < 2 && valid; index++) {
        valid = export_texture(wm_u8_find(&archive, texture_members[index]),
                               argv[2], &textures[index]);
    }
    const WmU8Entry *layout = wm_u8_find(&archive, layout_member);
    const WmU8Entry *animation = wm_u8_find(&archive, animation_member);
    WmResourceAnimation clip = {"my_BackToWiiMenu", animation->data,
                                animation->size};
    char *json = NULL;
    size_t json_size = 0;
    char error[160] = {0};
    if (valid) valid = wm_brlyt_to_json_with_source(
        layout->data, layout->size, "my_BackToWiiMenu", "restart",
        "system-menu-executable/embedded-restart/arc/blyt/my_BackToWiiMenu.brlyt",
        textures, 2, &clip, 1, &json, &json_size, error, sizeof(error));
    if (valid) valid = write_json(argv[2], json, json_size);
    if (!valid)
        fprintf(stderr, "Could not export embedded restart resources: %s\n", error);
    free(json);
    for (size_t index = 0; index < 2; index++) {
        free((char *)textures[index].name);
        free((char *)textures[index].url);
        free((char *)textures[index].source);
    }
    wm_u8_free(&archive);
    free(data);
    if (!valid) return 1;
    puts("Exported the original Back-to-Wii-Menu layout and textures.");
    return 0;
}
