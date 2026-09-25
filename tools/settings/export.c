#include "gif.h"
#include "png.h"

#include "wii_menu/image.h"
#include "wii_menu/resource_ash.h"
#include "wii_menu/resource_tpl.h"
#include "wii_menu/resource_u8.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { SETTINGS_EXPORT_PATH_CAPACITY = 4096 };
enum { SETTINGS_MAX_IMAGE_SOURCE = 16 * 1024 * 1024 };

typedef struct SettingsArtwork {
    const char *source;
    const char *name;
} SettingsArtwork;

/* These original raster components are shared by the Settings index,
 * category rows, and Widescreen detail. Only decoded local assets are
 * emitted; no WAD payload is committed. */
static const SettingsArtwork artwork[] = {
    {"FIX/COMMON/BG/BG_common.gif", "background"},
    {"FIX/COMMON/BG/Tab_White_L.gif", "title-tab"},
    {"FIX/COMMON/BG/Tab_gray.gif", "tab-gray"},
    {"FIX/COMMON/BG/Tab_gray02.gif", "tab-gray-nested"},
    {"FIX/COMMON/BG/Tab_Darkgray.gif", "tab-dark-gray"},
    {"FIX/COMMON/BG/Tab_Middle_gray.gif", "tab-middle-gray"},
    {"FIX/COMMON/BG/Tab_Middle_gray02.gif", "tab-middle-gray-nested"},
    {"FIX/COMMON/BG/List_flame_L.gif", "choice-left"},
    {"FIX/COMMON/BG/List_flame_R.gif", "choice-right"},
    {"FIX/COMMON/BG/List_flame_M_L.gif", "country-choice-left"},
    {"FIX/COMMON/BG/List_flame_M_R.gif", "country-choice-right"},
    {"FIX/COMMON/BG/TV_flame_L.gif", "tv-choice-left"},
    {"FIX/COMMON/BG/TV_flame_R.gif", "tv-choice-right"},
    {"FIX/COMMON/BG/Flame_L.gif", "position-flame-left"},
    {"FIX/COMMON/BG/Flame_R.gif", "position-flame-right"},
    {"FIX/COMMON/BTN/Btn_List03.gif", "index-row"},
    {"FIX/COMMON/BTN/List03_on.png", "index-row-focus"},
    {"FIX/COMMON/BTN/Btn_List.gif", "large-row"},
    {"FIX/COMMON/BTN/Btn_List_bunkatu.gif", "connection-split-row"},
    {"FIX/COMMON/BTN/Btn_List_M.gif", "small-row"},
    {"FIX/COMMON/BTN/List_M_on.png", "small-row-focus"},
    {"FIX/COMMON/List_Icon/Icon_AOSS.png", "aoss-icon"},
    {"FIX/COMMON/BTN/Btn_List_Dark.gif", "large-row-disabled"},
    {"FIX/COMMON/BTN/Btn_List06.gif", "country-row"},
    {"FIX/COMMON/BTN/List06_on.png", "country-row-focus"},
    {"FIX/COMMON/BTN/4_3_Btn.gif", "widescreen-standard"},
    {"FIX/COMMON/BTN/4_3_on.png", "widescreen-standard-focus"},
    {"FIX/COMMON/BTN/16_9_Btn.gif", "widescreen-wide"},
    {"FIX/COMMON/BTN/16_9_on.png", "widescreen-wide-focus"},
    {"FIX/COMMON/BTN/List_on.png", "large-row-focus"},
    {"FIX/COMMON/BTN/Under.gif", "footer-button"},
    {"FIX/COMMON/BTN/Under_on.png", "footer-button-focus"},
    {"FIX/COMMON/BTN/Under_Red_on.png", "footer-button-red-focus"},
    {"FIX/COMMON/BTN/Allow_L_off.gif", "arrow-left"},
    {"FIX/COMMON/BTN/Allow_L_on.gif", "arrow-left-focus"},
    {"FIX/COMMON/BTN/Allow_R_off.gif", "arrow-right"},
    {"FIX/COMMON/BTN/Allow_R_on.gif", "arrow-right-focus"},
    {"FIX/COMMON/BTN/Allow_U_off.gif", "arrow-up"},
    {"FIX/COMMON/BTN/Allow_U_on.gif", "arrow-up-focus"},
    {"FIX/COMMON/BTN/Allow_D_off.gif", "arrow-down"},
    {"FIX/COMMON/BTN/Allow_D_on.gif", "arrow-down-focus"},
    {"FIX/COMMON/BG/DPD_Minus_btn.gif", "sensitivity-minus"},
    {"FIX/COMMON/BG/DPD_PMGauge.gif", "sensitivity-gauge"},
    {"FIX/COMMON/BG/DPD_Plus_btn.gif", "sensitivity-plus"},
    {"FIX/COMMON/BTN/DPD_Rank01.gif", "sensitivity-rank-1"},
    {"FIX/COMMON/BTN/DPD_Rank02.gif", "sensitivity-rank-2"},
    {"FIX/COMMON/BTN/DPD_Rank03.gif", "sensitivity-rank-3"},
    {"FIX/COMMON/BTN/DPD_Rank04.gif", "sensitivity-rank-4"},
    {"FIX/COMMON/BTN/DPD_Rank05.gif", "sensitivity-rank-5"},
    {"FIX/COMMON/List_Icon/Icon_Index_Page_off.gif", "page-off"},
    {"FIX/COMMON/List_Icon/Icon_Index_Page_on.gif", "page-on"}
};

static uint8_t *read_file(const char *path, size_t *size) {
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc(length ? (size_t)length : 1);
    if (!bytes || fread(bytes, 1, (size_t)length, file) != (size_t)length) {
        free(bytes);
        fclose(file);
        return NULL;
    }
    if (fclose(file) != 0) {
        free(bytes);
        return NULL;
    }
    *size = (size_t)length;
    return bytes;
}

static bool make_directory(const char *path) {
    if (mkdir(path, 0700) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat metadata;
    return stat(path, &metadata) == 0 && S_ISDIR(metadata.st_mode);
}

static bool output_path(char *path, size_t capacity,
                        const char *directory, const char *name) {
    int length = snprintf(path, capacity, "%s/textures/settings_html/%s.wmra",
                          directory, name);
    return length > 0 && (size_t)length < capacity;
}

static bool tile_artwork(WmImage *image, unsigned source_width,
                         unsigned source_height, unsigned output_width,
                         unsigned output_height) {
    if (!image || !image->pixels || image->width != source_width ||
        image->height != source_height) return false;
    WmImage tiled = {
        .width = output_width,
        .height = output_height,
        .pixels = malloc((size_t)output_width * output_height * 4u)
    };
    if (!tiled.pixels) return false;
    for (unsigned y = 0; y < output_height; y++) {
        for (unsigned x = 0; x < output_width; x++) {
            memcpy(tiled.pixels + ((size_t)y * output_width + x) * 4u,
                   image->pixels + ((size_t)(y % source_height) *
                                    source_width + x % source_width) * 4u,
                   4u);
        }
    }
    wm_image_free(image);
    *image = tiled;
    return true;
}

static bool sensitivity_dimensions_valid(const char *name,
                                         const WmImage *image) {
    if (strcmp(name, "sensitivity-minus") == 0 ||
        strcmp(name, "sensitivity-plus") == 0)
        return image->width == 32 && image->height == 32;
    if (strcmp(name, "sensitivity-gauge") == 0)
        return image->width == 296 && image->height == 48;
    if (strncmp(name, "sensitivity-rank-", 17) == 0)
        return image->width == 56 && image->height == 56;
    return true;
}

static bool country_dimensions_valid(const char *name,
                                     const WmImage *image) {
    if (strcmp(name, "country-choice-left") == 0 ||
        strcmp(name, "country-choice-right") == 0)
        return image->width == 24 && image->height == 64;
    if (strcmp(name, "country-row") == 0 ||
        strcmp(name, "country-row-focus") == 0)
        return image->width == 432 && image->height == 56;
    return true;
}

static bool other_dimensions_valid(const char *name,
                                   const WmImage *image) {
    if (strcmp(name, "tab-dark-gray") == 0 ||
        strcmp(name, "tab-middle-gray-nested") == 0)
        return image->width == 32 && image->height == 26;
    if (strcmp(name, "connection-split-row") == 0)
        return image->width == 400 && image->height == 76;
    if (strcmp(name, "small-row") == 0 ||
        strcmp(name, "small-row-focus") == 0)
        return image->width == 168 && image->height == 76;
    if (strcmp(name, "aoss-icon") == 0)
        return image->width == 56 && image->height == 56;
    if (strcmp(name, "footer-button-red-focus") == 0)
        return image->width == 272 && image->height == 72;
    return true;
}

/* The Settings archive wraps individual GIFs in Nintendo type-0x10 LZ77.
 * The output bound is intentionally far below the full archive bound. */
static bool unwrap_image(const WmU8Entry *entry, uint8_t **output,
                         size_t *output_size) {
    if (!entry || entry->size < 8 ||
        memcmp(entry->data, "LZ77", 4) != 0 ||
        entry->data[4] != 0x10) return false;
    const uint8_t *source = entry->data + 4;
    size_t source_size = entry->size - 4;
    size_t length = (size_t)source[1] | ((size_t)source[2] << 8) |
                    ((size_t)source[3] << 16);
    if (!length || length > SETTINGS_MAX_IMAGE_SOURCE) return false;
    uint8_t *decoded = malloc(length);
    if (!decoded) return false;
    size_t offset = 4;
    size_t used = 0;
    while (used < length) {
        if (offset >= source_size) break;
        uint8_t flags = source[offset++];
        for (int bit = 7; bit >= 0 && used < length; bit--) {
            if (flags & (1u << bit)) {
                if (source_size - offset < 2) goto fail;
                unsigned word = ((unsigned)source[offset] << 8) |
                                source[offset + 1];
                offset += 2;
                size_t count = (word >> 12) + 3;
                size_t distance = (word & 0x0fffu) + 1;
                if (distance > used) goto fail;
                for (size_t index = 0; index < count && used < length;
                     index++) {
                    decoded[used] = decoded[used - distance];
                    used++;
                }
            } else {
                if (offset >= source_size) goto fail;
                decoded[used++] = source[offset++];
            }
        }
    }
    if (used != length) goto fail;
    *output = decoded;
    *output_size = length;
    return true;
fail:
    free(decoded);
    return false;
}

static bool export_artwork(const WmU8Archive *archive,
                           const char *directory) {
    for (size_t index = 0; index < sizeof(artwork) / sizeof(artwork[0]);
         index++) {
        const WmU8Entry *entry = wm_u8_find(archive, artwork[index].source);
        if (!entry) return false;
        uint8_t *source = NULL;
        size_t source_size = 0;
        WmImage decoded = {0};
        size_t source_name_size = strlen(artwork[index].source);
        bool png = source_name_size >= 4 &&
                   strcmp(artwork[index].source + source_name_size - 4,
                          ".png") == 0;
        if (!unwrap_image(entry, &source, &source_size) ||
            !(png
                ? wm_settings_png_decode(source, source_size, &decoded)
                : wm_settings_gif_decode(source, source_size, &decoded))) {
            fprintf(stderr, "Invalid Settings image: %s\n",
                    artwork[index].source);
            free(source);
            return false;
        }
        free(source);
        if (!sensitivity_dimensions_valid(artwork[index].name, &decoded) ||
            !country_dimensions_valid(artwork[index].name, &decoded) ||
            !other_dimensions_valid(artwork[index].name, &decoded)) {
            fprintf(stderr, "Unexpected Settings image size: %s\n",
                    artwork[index].source);
            wm_image_free(&decoded);
            return false;
        }
        if (strcmp(artwork[index].name, "background") == 0) {
            /* CSS repeats the eight-pixel GIF across the 608-pixel document.
             * Expand once at preparation, avoiding 76 texture submissions in
             * every Settings frame on the GLES2 target. */
            if (!tile_artwork(&decoded, 8, 456, 608, 456)) {
                wm_image_free(&decoded);
                return false;
            }
        }
        char path[SETTINGS_EXPORT_PATH_CAPACITY];
        bool written = output_path(path, sizeof(path), directory,
                                   artwork[index].name) &&
                       wm_image_write(path, &decoded);
        wm_image_free(&decoded);
        if (!written) return false;
    }
    return true;
}

static bool export_side_panel(const WmU8Archive *outer,
                              const char *directory) {
    const WmU8Entry *entry = wm_u8_find(outer, "html/BG_16x9.tpl");
    if (!entry) return false;
    WmTpl panel = {0};
    char error[160] = {0};
    if (!wm_tpl_decode(entry->data, entry->size, &panel,
                       error, sizeof(error))) {
        fprintf(stderr, "Settings side panel: %s\n", error);
        return false;
    }
    char path[SETTINGS_EXPORT_PATH_CAPACITY];
    bool valid = panel.count >= 1 && panel.images[0].width == 112 &&
                 panel.images[0].height == 456 &&
                 output_path(path, sizeof(path), directory, "side-panel");
    if (valid) {
        WmImage image = {
            .width = panel.images[0].width,
            .height = panel.images[0].height,
            .pixels = panel.images[0].rgba
        };
        valid = wm_image_write(path, &image);
    }
    wm_tpl_free(&panel);
    return valid;
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s RESOURCE_97_APP OUTPUT_ASSETS\n", argv[0]);
        return 2;
    }
    size_t input_size = 0;
    uint8_t *input = read_file(argv[1], &input_size);
    WmU8Archive outer = {0};
    char error[160] = {0};
    bool valid = input && wm_u8_parse(input, input_size,
                                      &outer, error, sizeof(error));
    if (!valid) {
        fprintf(stderr, "Settings resource archive: %s\n", error);
        free(input);
        return 1;
    }
    const WmU8Entry *entry = wm_u8_find(&outer, "html/US2/iplsetting.ash");
    uint8_t *decoded = NULL;
    size_t decoded_size = 0;
    WmU8Archive settings = {0};
    if (!entry || !wm_ash_decode(entry->data, entry->size,
                                  &decoded, &decoded_size,
                                  error, sizeof(error)) ||
        !wm_u8_parse(decoded, decoded_size, &settings,
                     error, sizeof(error))) {
        fprintf(stderr, "Settings HTML archive: %s\n", error);
        wm_u8_free(&outer);
        free(input);
        free(decoded);
        return 1;
    }
    char textures[SETTINGS_EXPORT_PATH_CAPACITY];
    char directory[SETTINGS_EXPORT_PATH_CAPACITY];
    int length = snprintf(textures, sizeof(textures), "%s/textures", argv[2]);
    int settings_length = snprintf(directory, sizeof(directory),
                                   "%s/textures/settings_html", argv[2]);
    valid = length > 0 && (size_t)length < sizeof(textures) &&
            settings_length > 0 &&
            (size_t)settings_length < sizeof(directory) &&
            make_directory(argv[2]) && make_directory(textures) &&
            make_directory(directory) &&
            export_artwork(&settings, argv[2]) &&
            export_side_panel(&outer, argv[2]);
    wm_u8_free(&settings);
    free(decoded);
    wm_u8_free(&outer);
    free(input);
    if (!valid) {
        fputs("Could not export local Settings artwork.\n", stderr);
        return 1;
    }
    puts("Exported original Settings artwork from local WAD.");
    return 0;
}
