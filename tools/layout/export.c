#include "atomic_file.h"
#include "export_directory.h"

#include "wii_menu/render/image.h"
#include "wii_menu/resources/resource_ash.h"
#include "wii_menu/resources/resource_bmg.h"
#include "wii_menu/resources/resource_layout.h"
#include "wii_menu/resources/resource_tpl.h"
#include "wii_menu/resources/resource_u8.h"
#include "wii_menu/support/regular_file.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { WM_EXPORT_PATH_CAPACITY = 4096, WM_EXPORT_MAX_SOURCE = 128 * 1024 * 1024 };

static bool ends_with(const char *value, const char *suffix) {
    size_t value_size = strlen(value);
    size_t suffix_size = strlen(suffix);
    return value_size >= suffix_size &&
           strcmp(value + value_size - suffix_size, suffix) == 0;
}

static bool package_name(const char *path, char name[128]) {
    static const char prefix[] = "layout/common/";
    if (strncmp(path, prefix, sizeof(prefix) - 1) != 0 || !ends_with(path, ".ash")) {
        return false;
    }
    size_t length = strlen(path) - (sizeof(prefix) - 1) - 4;
    if (length == 0 || length >= 128) {
        return false;
    }
    const char *start = path + sizeof(prefix) - 1;
    for (size_t index = 0; index < length; index++) {
        char value = start[index];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-')) {
            return false;
        }
    }
    memcpy(name, start, length);
    name[length] = '\0';
    return true;
}

static bool resource_stem(const char *path, const char *extension, char stem[128],
                          char basename[128]) {
    if (!ends_with(path, extension)) {
        return false;
    }
    const char *last_slash = strrchr(path, '/');
    const char *start = last_slash != NULL ? last_slash + 1 : path;
    size_t name_size = strlen(start);
    size_t extension_size = strlen(extension);
    if (name_size <= extension_size || name_size >= 128) {
        return false;
    }
    for (size_t index = 0; index < name_size - extension_size; index++) {
        char value = start[index];
        if (!((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
              (value >= '0' && value <= '9') || value == '_' || value == '-' ||
              value == '.')) {
            return false;
        }
    }
    memcpy(stem, start, name_size - extension_size);
    stem[name_size - extension_size] = '\0';
    memcpy(basename, start, name_size + 1);
    return true;
}

static bool write_path(char output[WM_EXPORT_PATH_CAPACITY], const char *directory,
                       const char *subdirectory, const char *filename) {
    int length = snprintf(output, WM_EXPORT_PATH_CAPACITY, "%s/%s/%s", directory,
                          subdirectory, filename);
    return length >= 0 && length < WM_EXPORT_PATH_CAPACITY;
}

static bool ensure_package_directories(const char *output, const char *package) {
    char relative[160];
    int length = snprintf(relative, sizeof(relative), "layouts/%s", package);
    if (length < 0 || length >= (int)sizeof(relative) ||
        !wm_export_directory_root(output, 0755) ||
        !wm_export_directory_child(output, "layouts", 0755) ||
        !wm_export_directory_child(output, "textures", 0755) ||
        !wm_export_directory_child(output, relative, 0755)) {
        return false;
    }
    length = snprintf(relative, sizeof(relative), "textures/%s", package);
    return length >= 0 && length < (int)sizeof(relative) &&
           wm_export_directory_child(output, relative, 0755);
}

static int compare_entries(const void *left, const void *right) {
    const WmU8Entry *a = left;
    const WmU8Entry *b = right;
    return strcmp(a->path, b->path);
}

static const char *resource_archive_path(const WmU8Entry *item,
                                         const WmU8Entry *common_entry,
                                         const WmU8Entry *localized_entry,
                                         const WmU8Archive *localized) {
    /* Merging copies entries shallowly, so path identity still marks an
     * entry that came from the localized archive. */
    if (localized_entry != NULL) {
        const WmU8Entry *candidate = wm_u8_find(localized, item->path);
        if (candidate != NULL && candidate->path == item->path) {
            return localized_entry->path;
        }
    }
    return common_entry->path;
}

typedef struct {
    const WmU8Entry *common_entry;
    const WmU8Entry *localized_entry;
    const WmU8Archive *localized;
    const char *output;
    const char *package;
    WmResourceTexture *textures;
    WmResourceAnimation *animations;
    size_t texture_count;
    size_t animation_count;
    size_t *texture_total;
    size_t *layout_total;
    char *error;
    size_t error_capacity;
} PackageCatalog;

static bool catalog_source_path(const PackageCatalog *catalog, const WmU8Entry *item,
                                char source[WM_EXPORT_PATH_CAPACITY]) {
    int length =
        snprintf(source, WM_EXPORT_PATH_CAPACITY, "%s/%s",
                 resource_archive_path(item, catalog->common_entry,
                                       catalog->localized_entry, catalog->localized),
                 item->path);
    return length >= 0 && length < WM_EXPORT_PATH_CAPACITY;
}

static bool catalog_add_animation(PackageCatalog *catalog, const WmU8Entry *item,
                                  const char *stem) {
    char *name = malloc(strlen(stem) + 1);
    if (!name)
        return false;
    strcpy(name, stem);
    catalog->animations[catalog->animation_count++] =
        (WmResourceAnimation){.name = name, .data = item->data, .size = item->size};
    return true;
}

static bool catalog_write_texture_image(const PackageCatalog *catalog, const WmTpl *tpl,
                                        const char *stem, size_t image_index) {
    char filename[160];
    int filename_length =
        snprintf(filename, sizeof(filename),
                 image_index == 0 ? "%s.wmra" : "%s-%zu.wmra", stem, image_index);
    char subdirectory[160];
    int directory_length =
        snprintf(subdirectory, sizeof(subdirectory), "textures/%s", catalog->package);
    char path[WM_EXPORT_PATH_CAPACITY];
    if (filename_length < 0 || filename_length >= (int)sizeof(filename) ||
        directory_length < 0 || directory_length >= (int)sizeof(subdirectory) ||
        !write_path(path, catalog->output, subdirectory, filename))
        return false;

    WmImage converted = {tpl->images[image_index].width,
                         tpl->images[image_index].height,
                         tpl->images[image_index].rgba};
    if (!wm_image_write(path, &converted)) {
        fprintf(stderr, "Could not write a local texture.\n");
        return false;
    }
    (*catalog->texture_total)++;
    return true;
}

static bool catalog_record_texture(PackageCatalog *catalog, const WmTpl *tpl,
                                   const char *basename, const char *stem,
                                   const char *source) {
    char url[WM_EXPORT_PATH_CAPACITY];
    int length =
        snprintf(url, sizeof(url), "textures/%s/%s.png", catalog->package, stem);
    if (length < 0 || length >= (int)sizeof(url))
        return false;

    char *owned_name = malloc(strlen(basename) + 1);
    char *owned_url = malloc(strlen(url) + 1);
    char *owned_source = malloc(strlen(source) + 1);
    if (!owned_name || !owned_url || !owned_source) {
        free(owned_name);
        free(owned_url);
        free(owned_source);
        return false;
    }
    strcpy(owned_name, basename);
    strcpy(owned_url, url);
    strcpy(owned_source, source);
    catalog->textures[catalog->texture_count++] =
        (WmResourceTexture){owned_name,
                            owned_url,
                            tpl->images[0].width,
                            tpl->images[0].height,
                            tpl->images[0].format,
                            owned_source};
    return true;
}

static bool catalog_export_texture(PackageCatalog *catalog, const WmU8Entry *item,
                                   const char *stem, const char *basename) {
    char source[WM_EXPORT_PATH_CAPACITY];
    if (!catalog_source_path(catalog, item, source))
        return false;

    WmTpl tpl = {0};
    if (!wm_tpl_decode(item->data, item->size, &tpl, catalog->error,
                       catalog->error_capacity)) {
        fprintf(stderr, "TPL decode failed: %s\n", catalog->error);
        return false;
    }
    bool valid = true;
    for (size_t image = 0; image < tpl.count; image++) {
        if (!catalog_write_texture_image(catalog, &tpl, stem, image)) {
            valid = false;
            break;
        }
    }
    if (valid && tpl.count != 0)
        valid = catalog_record_texture(catalog, &tpl, basename, stem, source);
    wm_tpl_free(&tpl);
    return valid;
}

static bool catalog_export_layout(PackageCatalog *catalog, const WmU8Entry *item,
                                  const char *stem) {
    char source[WM_EXPORT_PATH_CAPACITY];
    char *json = NULL;
    size_t json_size = 0;
    bool valid =
        catalog_source_path(catalog, item, source) &&
        wm_brlyt_to_json_with_source(
            item->data, item->size, stem, catalog->package, source, catalog->textures,
            catalog->texture_count, catalog->animations, catalog->animation_count,
            &json, &json_size, catalog->error, catalog->error_capacity);
    if (!valid) {
        fprintf(stderr, "BRLYT export failed: %s\n", catalog->error);
        free(json);
        return false;
    }

    char filename[160];
    char subdirectory[160];
    int filename_size = snprintf(filename, sizeof(filename), "%s.json", stem);
    int directory_size =
        snprintf(subdirectory, sizeof(subdirectory), "layouts/%s", catalog->package);
    char path[WM_EXPORT_PATH_CAPACITY];
    valid = filename_size >= 0 && filename_size < (int)sizeof(filename) &&
            directory_size >= 0 && directory_size < (int)sizeof(subdirectory) &&
            write_path(path, catalog->output, subdirectory, filename) &&
            wm_atomic_file_replace(path, json, json_size);
    free(json);
    if (!valid) {
        fprintf(stderr, "Could not write a local layout.\n");
        return false;
    }
    (*catalog->layout_total)++;
    return true;
}

static bool export_package(const WmU8Entry *entry, const WmU8Entry *localized_entry,
                           const char *output, const char *package,
                           size_t *layout_total, size_t *texture_total) {
    char error[160] = {0};
    uint8_t *decoded = NULL;
    uint8_t *localized_decoded = NULL;
    size_t decoded_size = 0;
    WmU8Archive archive = {0};
    WmU8Archive localized = {0};
    WmU8Entry *merged = NULL;
    WmResourceTexture *textures = NULL;
    WmResourceAnimation *animations = NULL;
    PackageCatalog catalog = {0};
    size_t merged_count = 0;
    bool valid = false;

    if (!wm_ash_decode(entry->data, entry->size, &decoded, &decoded_size, error,
                       sizeof(error))) {
        fprintf(stderr, "ASH decode failed: %s\n", error);
        goto release_package;
    }
    if (!wm_u8_parse(decoded, decoded_size, &archive, error, sizeof(error))) {
        fprintf(stderr, "Nested U8 parse failed: %s\n", error);
        goto release_package;
    }
    if (localized_entry != NULL) {
        size_t localized_size = 0;
        if (!wm_ash_decode(localized_entry->data, localized_entry->size,
                           &localized_decoded, &localized_size, error, sizeof(error)) ||
            !wm_u8_parse(localized_decoded, localized_size, &localized, error,
                         sizeof(error))) {
            fprintf(stderr, "Localized archive decode failed: %s\n", error);
            goto release_package;
        }
    }
    if (archive.count >= SIZE_MAX - localized.count) {
        fprintf(stderr, "Resource archive has too many entries.\n");
        goto release_package;
    }
    merged = calloc(archive.count + localized.count + 1, sizeof(*merged));
    if (merged == NULL) {
        fprintf(stderr, "Out of memory merging local resources.\n");
        goto release_package;
    }
    merged_count = archive.count;
    if (archive.count > 0)
        memcpy(merged, archive.entries, archive.count * sizeof(*merged));
    for (size_t index = 0; index < localized.count; index++) {
        const WmU8Entry *item = &localized.entries[index];
        size_t match = 0;
        while (match < merged_count && strcmp(merged[match].path, item->path) != 0) {
            match++;
        }
        merged[match] = *item;
        if (match == merged_count) {
            merged_count++;
        }
    }
    qsort(merged, merged_count, sizeof(*merged), compare_entries);
    if (!ensure_package_directories(output, package)) {
        fprintf(stderr, "Could not create the local output directories.\n");
        goto release_package;
    }

    textures = calloc(merged_count + 1, sizeof(*textures));
    animations = calloc(merged_count + 1, sizeof(*animations));
    if (textures == NULL || animations == NULL) {
        fprintf(stderr, "Out of memory preparing the package.\n");
        goto release_package;
    }
    catalog = (PackageCatalog){.common_entry = entry,
                               .localized_entry = localized_entry,
                               .localized = &localized,
                               .output = output,
                               .package = package,
                               .textures = textures,
                               .animations = animations,
                               .texture_total = texture_total,
                               .layout_total = layout_total,
                               .error = error,
                               .error_capacity = sizeof(error)};
    valid = true;
    for (size_t index = 0; index < merged_count && valid; index++) {
        const WmU8Entry *item = &merged[index];
        char stem[128];
        char basename[128];
        if (resource_stem(item->path, ".brlan", stem, basename))
            valid = catalog_add_animation(&catalog, item, stem);
        else if (resource_stem(item->path, ".tpl", stem, basename))
            valid = catalog_export_texture(&catalog, item, stem, basename);
    }
    for (size_t index = 0; index < merged_count && valid; index++) {
        const WmU8Entry *item = &merged[index];
        char stem[128];
        char basename[128];
        if (resource_stem(item->path, ".brlyt", stem, basename))
            valid = catalog_export_layout(&catalog, item, stem);
    }

release_package:
    for (size_t index = 0; index < catalog.texture_count; index++) {
        free((char *)textures[index].name);
        free((char *)textures[index].url);
        free((char *)textures[index].source);
    }
    for (size_t index = 0; index < catalog.animation_count; index++) {
        free((char *)animations[index].name);
    }
    free(textures);
    free(animations);
    free(merged);
    wm_u8_free(&localized);
    wm_u8_free(&archive);
    free(localized_decoded);
    free(decoded);
    return valid;
}

static bool export_fonts(const WmU8Archive *outer, const char *output,
                         size_t *font_total) {
    const WmU8Entry *entry = wm_u8_find(outer, "font/font.ash");
    if (entry == NULL) {
        return true;
    }
    char error[160] = {0};
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    if (!wm_ash_decode(entry->data, entry->size, &decoded, &decoded_size, error,
                       sizeof(error))) {
        fprintf(stderr, "Font archive decode failed: %s\n", error);
        return false;
    }
    WmU8Archive archive = {0};
    if (!wm_u8_parse(decoded, decoded_size, &archive, error, sizeof(error))) {
        fprintf(stderr, "Font archive parse failed: %s\n", error);
        free(decoded);
        return false;
    }
    char directory[WM_EXPORT_PATH_CAPACITY];
    bool valid = wm_export_directory_root(output, 0755) &&
                 write_path(directory, output, "fonts", "") &&
                 wm_export_directory_child(output, "fonts", 0755);
    for (size_t index = 0; index < archive.count && valid; index++) {
        const WmU8Entry *item = &archive.entries[index];
        char stem[128];
        char basename[128];
        if (!resource_stem(item->path, ".brfnt", stem, basename)) {
            continue;
        }
        char path[WM_EXPORT_PATH_CAPACITY];
        valid = write_path(path, output, "fonts", basename) &&
                wm_atomic_file_replace(path, item->data, item->size);
        if (valid) {
            (*font_total)++;
        }
    }
    if (!valid) {
        fprintf(stderr, "Could not write the local font resources.\n");
    }
    wm_u8_free(&archive);
    free(decoded);
    return valid;
}

static bool export_messages(const WmU8Archive *outer, const char *output,
                            const char *language, size_t *message_total) {
    char source[64];
    int source_length =
        snprintf(source, sizeof(source), "message/%s/ipl_common.bmg", language);
    if (source_length < 0 || source_length >= (int)sizeof(source))
        return false;
    const WmU8Entry *entry = wm_u8_find(outer, source);
    if (!entry) {
        fprintf(stderr, "The selected locale has no common BMG messages.\n");
        return false;
    }
    char error[160] = {0};
    WmBmg *messages = wm_bmg_parse(entry->data, entry->size, error, sizeof(error));
    if (!messages) {
        fprintf(stderr, "Common BMG parse failed: %s\n", error);
        return false;
    }
    char path[WM_EXPORT_PATH_CAPACITY];
    char subdirectory[64];
    int sub_length =
        snprintf(subdirectory, sizeof(subdirectory), "messages/%s", language);
    bool valid = sub_length >= 0 && sub_length < (int)sizeof(subdirectory) &&
                 wm_export_directory_root(output, 0755) &&
                 write_path(path, output, "messages", "") &&
                 wm_export_directory_child(output, "messages", 0755) &&
                 write_path(path, output, subdirectory, "") &&
                 wm_export_directory_child(output, subdirectory, 0755) &&
                 write_path(path, output, subdirectory, "ipl_common.bmg") &&
                 wm_atomic_file_replace(path, entry->data, entry->size);
    if (!valid) {
        fprintf(stderr, "Could not write local BMG messages.\n");
    } else {
        *message_total = wm_bmg_count(messages);
    }
    wm_bmg_destroy(messages);
    return valid;
}

static bool language_code(const char *argument, char language[4]) {
    if (strlen(argument) != 3) {
        return false;
    }
    for (size_t index = 0; index < 3; index++) {
        char value = argument[index];
        if (value >= 'A' && value <= 'Z') {
            value = (char)(value - 'A' + 'a');
        }
        if (value < 'a' || value > 'z') {
            return false;
        }
        language[index] = value;
    }
    language[3] = '\0';
    return true;
}

int main(int argc, char **argv) {
    char language[4];
    if ((argc != 3 && argc != 4) ||
        !language_code(argc == 4 ? argv[3] : "eng", language)) {
        fprintf(
            stderr,
            "Usage: layout-export RESOURCE_APP LOCAL_OUTPUT_DIRECTORY [LANGUAGE]\n");
        return 2;
    }
    size_t input_size = 0;
    uint8_t *input = NULL;
    if (wm_regular_file_read_bytes(argv[1], 0, WM_EXPORT_MAX_SOURCE, &input,
                                   &input_size) != WM_REGULAR_FILE_OK) {
        fprintf(stderr, "Could not read the resource content.\n");
        return 1;
    }
    char error[160] = {0};
    WmU8Archive outer = {0};
    if (!wm_u8_parse(input, input_size, &outer, error, sizeof(error))) {
        fprintf(stderr, "Resource archive parse failed: %s\n", error);
        free(input);
        return 1;
    }
    size_t layouts = 0;
    size_t textures = 0;
    size_t fonts = 0;
    size_t messages = 0;
    bool valid = true;
    for (size_t index = 0; index < outer.count && valid; index++) {
        char package[128];
        if (!package_name(outer.entries[index].path, package)) {
            continue;
        }
        char localized_path[160];
        int path_size = snprintf(localized_path, sizeof(localized_path),
                                 "layout/%s/%s.ash", language, package);
        if (path_size < 0 || path_size >= (int)sizeof(localized_path)) {
            valid = false;
            break;
        }
        const WmU8Entry *localized = wm_u8_find(&outer, localized_path);
        valid = export_package(&outer.entries[index], localized, argv[2], package,
                               &layouts, &textures);
    }
    if (valid) {
        valid = export_fonts(&outer, argv[2], &fonts);
    }
    if (valid) {
        valid = export_messages(&outer, argv[2], language, &messages);
    }
    wm_u8_free(&outer);
    free(input);
    if (!valid || layouts == 0) {
        return 1;
    }
    printf("Exported %zu BRLYT layouts, %zu WMRA textures, %zu raw BRFNT fonts, and "
           "%zu BMG messages.\n",
           layouts, textures, fonts, messages);
    return 0;
}
