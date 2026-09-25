#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "wii_menu/font_cache.h"

#include "wii_menu/image.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
    WM_FONT_CACHE_MAX_FACES = 64,
    WM_FONT_CACHE_MAX_LAYOUTS = 256,
    WM_FONT_CACHE_MAX_FILE_BYTES = 64 * 1024 * 1024,
    WM_FONT_CACHE_MAX_RAM_BYTES = 64 * 1024 * 1024,
    WM_FONT_CACHE_MAX_TEXT_BYTES = 65536
};

static const char WM_DEFAULT_FONT[] =
    "RevoIpl_RodinNTLGPro_DB_48_IA4.brfnt";

typedef enum SheetState {
    SHEET_UNLOADED,
    SHEET_RESIDENT,
    SHEET_FAILED
} SheetState;

typedef struct CachedSheet {
    uint32_t texture;
    size_t bytes;
    uint64_t frame_used;
    uint64_t recent_use;
    SheetState state;
} CachedSheet;

typedef struct CachedLayout {
    struct CachedLayout *next;
    WmCachedFont *face;
    char *text;
    size_t text_length;
    WmFontPane pane;
    WmFontTextLayout *layout;
    uint64_t recent_use;
} CachedLayout;

struct WmCachedFont {
    WmFontCache *cache;
    char name[128];
    WmFont *font;
    CachedSheet *sheets;
    size_t sheet_count;
    size_t file_bytes;
};

struct WmFontCache {
    WmPlatform *platform;
    char *assets_root;
    WmCachedFont *faces[WM_FONT_CACHE_MAX_FACES];
    size_t face_count;
    size_t font_bytes;
    CachedLayout *layouts;
    size_t layout_count;
    size_t gpu_budget_bytes;
    size_t resident_bytes;
    size_t resident_sheets;
    uint64_t frame;
    uint64_t use_clock;
    uint64_t evictions;
};

static bool valid_font_name(const char *name) {
    if (!name) return false;
    size_t length = strnlen(name, 128);
    if (!length || length >= 128 || name[0] == '.' || strstr(name, "..")) return false;
    bool extension = (length > 6 && strcmp(name + length - 6, ".brfnt") == 0) ||
                     (length > 6 && strcmp(name + length - 6, ".brfna") == 0);
    if (!extension) return false;
    for (size_t index = 0; index < length; index++) {
        unsigned char ch = (unsigned char)name[index];
        if (!((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '_' || ch == '-' || ch == '.')) {
            return false;
        }
    }
    return true;
}

static bool path_within_root(const char *root, const char *path) {
    size_t length = strlen(root);
    if (length == 1 && root[0] == '/') return path[0] == '/' && path[1] != '\0';
    return strncmp(root, path, length) == 0 && path[length] == '/';
}

static char *font_path(const WmFontCache *cache, const char *name) {
    if (!valid_font_name(name)) return NULL;
    size_t root_length = strlen(cache->assets_root);
    size_t name_length = strlen(name);
    if (root_length > SIZE_MAX - name_length - 8) return NULL;
    char *candidate = malloc(root_length + name_length + 8);
    if (!candidate) return NULL;
    snprintf(candidate, root_length + name_length + 8, "%s/fonts/%s",
             cache->assets_root, name);
    char *canonical = realpath(candidate, NULL);
    free(candidate);
    if (!canonical) return NULL;
    if (!path_within_root(cache->assets_root, canonical)) {
        free(canonical);
        return NULL;
    }
    return canonical;
}

static WmFont *read_font(const char *path, size_t *file_bytes) {
    FILE *file = fopen(path, "rb");
    if (!file) return NULL;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return NULL; }
    long length = ftell(file);
    if (length <= 0 || length > WM_FONT_CACHE_MAX_FILE_BYTES ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *bytes = malloc((size_t)length);
    if (!bytes) { fclose(file); return NULL; }
    bool complete = fread(bytes, 1, (size_t)length, file) == (size_t)length;
    fclose(file);
    WmFont *font = NULL;
    if (complete) font = wm_font_decode(bytes, (size_t)length, NULL, 0);
    free(bytes);
    if (font) *file_bytes = (size_t)length;
    return font;
}

static uint64_t next_use(WmFontCache *cache) {
    if (cache->use_clock == UINT64_MAX) {
        for (size_t face = 0; face < cache->face_count; face++) {
            WmCachedFont *font = cache->faces[face];
            for (size_t sheet = 0; sheet < font->sheet_count; sheet++) {
                font->sheets[sheet].recent_use = 0;
            }
        }
        for (CachedLayout *entry = cache->layouts; entry; entry = entry->next) {
            entry->recent_use = 0;
        }
        cache->use_clock = 0;
    }
    return ++cache->use_clock;
}

WmFontCache *wm_font_cache_create(WmPlatform *platform, const char *assets_root,
                                   size_t gpu_budget_bytes) {
    if (!platform || !assets_root || !assets_root[0] || !gpu_budget_bytes) return NULL;
    char *canonical = realpath(assets_root, NULL);
    if (!canonical) return NULL;
    struct stat info;
    if (stat(canonical, &info) != 0 || !S_ISDIR(info.st_mode)) {
        free(canonical);
        return NULL;
    }
    WmFontCache *cache = calloc(1, sizeof(*cache));
    if (!cache) { free(canonical); return NULL; }
    cache->platform = platform;
    cache->assets_root = canonical;
    cache->gpu_budget_bytes = gpu_budget_bytes;
    cache->frame = 1;
    return cache;
}

void wm_font_cache_destroy(WmFontCache *cache) {
    if (!cache) return;
    CachedLayout *entry = cache->layouts;
    while (entry) {
        CachedLayout *next = entry->next;
        wm_font_text_layout_destroy(entry->layout);
        free(entry->text);
        free(entry);
        entry = next;
    }
    for (size_t index = 0; index < cache->face_count; index++) {
        WmCachedFont *face = cache->faces[index];
        for (size_t sheet = 0; sheet < face->sheet_count; sheet++) {
            if (face->sheets[sheet].state == SHEET_RESIDENT) {
                wm_platform_destroy_texture(cache->platform,
                                             face->sheets[sheet].texture);
            }
        }
        free(face->sheets);
        wm_font_destroy(face->font);
        free(face);
    }
    free(cache->assets_root);
    free(cache);
}

void wm_font_cache_begin_frame(WmFontCache *cache) {
    if (!cache) return;
    if (cache->frame == UINT64_MAX) {
        for (size_t index = 0; index < cache->face_count; index++) {
            WmCachedFont *face = cache->faces[index];
            for (size_t sheet = 0; sheet < face->sheet_count; sheet++) {
                face->sheets[sheet].frame_used = 0;
            }
        }
        cache->frame = 1;
    } else {
        cache->frame++;
    }
}

static WmCachedFont *find_or_load(WmFontCache *cache, const char *name) {
    if (!valid_font_name(name)) return NULL;
    for (size_t index = 0; index < cache->face_count; index++) {
        if (strcmp(cache->faces[index]->name, name) == 0) return cache->faces[index];
    }
    if (cache->face_count == WM_FONT_CACHE_MAX_FACES) return NULL;
    WmCachedFont *face = calloc(1, sizeof(*face));
    if (!face) return NULL;
    face->cache = cache;
    strcpy(face->name, name);
    char *path = font_path(cache, name);
    if (path) {
        size_t bytes = 0;
        WmFont *font = read_font(path, &bytes);
        free(path);
        if (font && bytes <= WM_FONT_CACHE_MAX_RAM_BYTES - cache->font_bytes) {
            face->sheet_count = wm_font_sheet_count(font);
            face->sheets = calloc(face->sheet_count ? face->sheet_count : 1,
                                  sizeof(*face->sheets));
            if (face->sheets) {
                face->font = font;
                face->file_bytes = bytes;
                cache->font_bytes += bytes;
            } else {
                wm_font_destroy(font);
            }
        } else {
            wm_font_destroy(font);
        }
    }
    cache->faces[cache->face_count++] = face;
    return face;
}

WmCachedFont *wm_font_cache_resolve(WmFontCache *cache, const char *font_name) {
    if (!cache) return NULL;
    WmCachedFont *face = find_or_load(cache, font_name);
    if (face && face->font) return face;
    if (font_name && strcmp(font_name, WM_DEFAULT_FONT) == 0) return NULL;
    face = find_or_load(cache, WM_DEFAULT_FONT);
    return face && face->font ? face : NULL;
}

const WmFont *wm_cached_font_resource(const WmCachedFont *face) {
    return face ? face->font : NULL;
}

float wm_font_cache_measure_text(void *context, const WmLayout *layout,
                                  const WmLayoutPaneState *pane,
                                  const char *utf8, size_t length) {
    (void)layout;
    if (!context || !pane || !utf8) return 0;
    WmCachedFont *face = wm_font_cache_resolve(context, pane->font_name);
    return wm_font_text_width_n(wm_cached_font_resource(face), utf8, length,
                                pane->font_size, pane->char_space);
}

static bool same_pane_geometry(const WmFontPane *a, const WmFontPane *b) {
    return a->size[0] == b->size[0] && a->size[1] == b->size[1] &&
           a->origin == b->origin && a->text_position == b->text_position &&
           a->font_size[0] == b->font_size[0] &&
           a->font_size[1] == b->font_size[1] &&
           a->char_space == b->char_space &&
           a->line_space == b->line_space && a->no_wrap == b->no_wrap;
}

static void evict_oldest_layout(WmFontCache *cache) {
    CachedLayout **oldest = NULL;
    for (CachedLayout **cursor = &cache->layouts; *cursor;
         cursor = &(*cursor)->next) {
        if (!oldest || (*cursor)->recent_use < (*oldest)->recent_use) {
            oldest = cursor;
        }
    }
    if (!oldest) return;
    CachedLayout *victim = *oldest;
    *oldest = victim->next;
    wm_font_text_layout_destroy(victim->layout);
    free(victim->text);
    free(victim);
    cache->layout_count--;
}

const WmFontTextLayout *wm_font_cache_layout(WmCachedFont *face,
                                             const char *utf8,
                                             const WmFontPane *pane) {
    if (!face || !face->font || !utf8 || !pane) return NULL;
    size_t length = strnlen(utf8, WM_FONT_CACHE_MAX_TEXT_BYTES + 1);
    if (length > WM_FONT_CACHE_MAX_TEXT_BYTES) return NULL;
    WmFontCache *cache = face->cache;
    for (CachedLayout *entry = cache->layouts; entry; entry = entry->next) {
        if (entry->face == face && entry->text_length == length &&
            same_pane_geometry(&entry->pane, pane) &&
            memcmp(entry->text, utf8, length + 1) == 0) {
            entry->recent_use = next_use(cache);
            return entry->layout;
        }
    }
    WmFontPane geometry = *pane;
    memset(geometry.top_color, 255, sizeof(geometry.top_color));
    memset(geometry.bottom_color, 255, sizeof(geometry.bottom_color));
    WmFontTextLayout *layout = wm_font_layout_pane(face->font, utf8, &geometry);
    if (!layout) return NULL;
    CachedLayout *entry = calloc(1, sizeof(*entry));
    if (!entry) { wm_font_text_layout_destroy(layout); return NULL; }
    entry->text = malloc(length + 1);
    if (!entry->text) {
        free(entry);
        wm_font_text_layout_destroy(layout);
        return NULL;
    }
    memcpy(entry->text, utf8, length + 1);
    entry->text_length = length;
    entry->face = face;
    entry->pane = geometry;
    entry->layout = layout;
    entry->recent_use = next_use(cache);
    if (cache->layout_count == WM_FONT_CACHE_MAX_LAYOUTS) evict_oldest_layout(cache);
    entry->next = cache->layouts;
    cache->layouts = entry;
    cache->layout_count++;
    return layout;
}

static CachedSheet *oldest_sheet(WmFontCache *cache,
                                  WmCachedFont **owner) {
    CachedSheet *oldest = NULL;
    for (size_t face_index = 0; face_index < cache->face_count; face_index++) {
        WmCachedFont *face = cache->faces[face_index];
        for (size_t index = 0; index < face->sheet_count; index++) {
            CachedSheet *sheet = &face->sheets[index];
            if (sheet->state != SHEET_RESIDENT || sheet->frame_used == cache->frame) {
                continue;
            }
            if (!oldest || sheet->recent_use < oldest->recent_use) {
                oldest = sheet;
                *owner = face;
            }
        }
    }
    return oldest;
}

static bool reserve_sheet_bytes(WmFontCache *cache, size_t bytes) {
    if (bytes > cache->gpu_budget_bytes) return false;
    while (cache->resident_bytes > cache->gpu_budget_bytes - bytes) {
        WmCachedFont *owner = NULL;
        CachedSheet *victim = oldest_sheet(cache, &owner);
        if (!victim || !owner) return false;
        wm_platform_destroy_texture(cache->platform, victim->texture);
        cache->resident_bytes -= victim->bytes;
        cache->resident_sheets--;
        victim->texture = 0;
        victim->state = SHEET_UNLOADED;
        cache->evictions++;
    }
    return true;
}

bool wm_font_cache_sheet(void *context, size_t sheet_index, uint32_t *texture) {
    if (texture) *texture = 0;
    WmCachedFont *face = context;
    if (!face || !face->font || !texture || sheet_index >= face->sheet_count) {
        return false;
    }
    WmFontCache *cache = face->cache;
    CachedSheet *sheet = &face->sheets[sheet_index];
    if (sheet->state == SHEET_RESIDENT) {
        sheet->frame_used = cache->frame;
        sheet->recent_use = next_use(cache);
        *texture = sheet->texture;
        return true;
    }
    if (sheet->state == SHEET_FAILED) return false;
    const WmFontSheetInfo *info = wm_font_sheet_info(face->font, sheet_index);
    if (!info) return false;
    uint64_t pixels = (uint64_t)info->width * info->height;
    if (pixels > SIZE_MAX / 4 || !reserve_sheet_bytes(cache, (size_t)pixels * 4)) {
        return false;
    }
    WmImage image = {0};
    if (!wm_font_decode_sheet(face->font, sheet_index, &image, NULL, 0)) {
        sheet->state = SHEET_FAILED;
        return false;
    }
    if (image.width != info->width || image.height != info->height) {
        wm_image_free(&image);
        sheet->state = SHEET_FAILED;
        return false;
    }
    for (size_t pixel = 0; pixel < (size_t)pixels; pixel++) {
        image.pixels[pixel * 4] = 255;
        image.pixels[pixel * 4 + 1] = 255;
        image.pixels[pixel * 4 + 2] = 255;
    }
    uint32_t handle = wm_platform_create_texture(cache->platform,
                                                  (int)image.width,
                                                  (int)image.height,
                                                  image.pixels);
    wm_image_free(&image);
    if (!handle) {
        sheet->state = SHEET_FAILED;
        return false;
    }
    sheet->texture = handle;
    sheet->bytes = (size_t)pixels * 4;
    sheet->frame_used = cache->frame;
    sheet->recent_use = next_use(cache);
    sheet->state = SHEET_RESIDENT;
    cache->resident_bytes += sheet->bytes;
    cache->resident_sheets++;
    *texture = handle;
    return true;
}

WmFontCacheStats wm_font_cache_stats(const WmFontCache *cache) {
    WmFontCacheStats stats = {0};
    if (!cache) return stats;
    for (size_t index = 0; index < cache->face_count; index++) {
        if (cache->faces[index]->font) stats.loaded_fonts++;
    }
    stats.resident_sheets = cache->resident_sheets;
    stats.resident_bytes = cache->resident_bytes;
    stats.cached_layouts = cache->layout_count;
    stats.evictions = cache->evictions;
    return stats;
}
