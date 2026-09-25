#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "wii_menu/texture_cache.h"

#include "wii_menu/image.h"
#include "wii_menu/layout_runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum { WM_MAX_URL_LENGTH = 1024, WM_MAX_TEXTURE_SOURCES = 8192 };

typedef enum WmTextureState {
    WM_TEXTURE_UNLOADED,
    WM_TEXTURE_RESIDENT,
    WM_TEXTURE_FAILED
} WmTextureState;

typedef struct WmTextureEntry {
    char *url;
    uint32_t handle;
    size_t bytes;
    uint64_t frame_used;
    uint64_t recent_use;
    WmTextureState state;
} WmTextureEntry;

struct WmTextureCache {
    WmPlatform *platform;
    char *raw_root;
    WmTextureEntry *entries;
    size_t entry_count;
    size_t entry_capacity;
    size_t budget_bytes;
    size_t resident_bytes;
    uint64_t frame;
    uint64_t use_clock;
    uint64_t evictions;
};

static bool valid_relative_png_url(const char *url, size_t *length)
{
    if (!url || !url[0]) {
        return false;
    }
    size_t size = strnlen(url, WM_MAX_URL_LENGTH + 1);
    if (size < 5 || size > WM_MAX_URL_LENGTH ||
        strcmp(url + size - 4, ".png") != 0) {
        return false;
    }

    size_t segment_start = 0;
    for (size_t index = 0; index <= size; ++index) {
        unsigned char character = (unsigned char)url[index];
        if (character == '/' || character == '\0') {
            size_t segment_length = index - segment_start;
            if (segment_length == 0 ||
                (segment_length == 1 && url[segment_start] == '.') ||
                (segment_length == 2 && url[segment_start] == '.' &&
                 url[segment_start + 1] == '.')) {
                return false;
            }
            segment_start = index + 1;
        } else if (character < 32 || character == 127 || character == '\\' ||
                   character == ':' || character == '?' || character == '#' ||
                   character == '%') {
            return false;
        }
    }
    *length = size;
    return true;
}

static WmTextureEntry *find_entry(WmTextureCache *cache, const char *url,
                                   size_t *insertion_index)
{
    size_t low = 0;
    size_t high = cache->entry_count;
    while (low < high) {
        size_t middle = low + (high - low) / 2;
        int comparison = strcmp(cache->entries[middle].url, url);
        if (comparison < 0) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    *insertion_index = low;
    if (low < cache->entry_count && strcmp(cache->entries[low].url, url) == 0) {
        return &cache->entries[low];
    }
    return NULL;
}

static WmTextureEntry *insert_entry(WmTextureCache *cache, const char *url,
                                     size_t url_length, size_t index)
{
    if (cache->entry_count >= WM_MAX_TEXTURE_SOURCES) {
        return NULL;
    }
    char *copy = malloc(url_length + 1);
    if (!copy) {
        return NULL;
    }
    memcpy(copy, url, url_length + 1);

    if (cache->entry_count == cache->entry_capacity) {
        size_t capacity = cache->entry_capacity ? cache->entry_capacity * 2 : 64;
        if (capacity > WM_MAX_TEXTURE_SOURCES) {
            capacity = WM_MAX_TEXTURE_SOURCES;
        }
        WmTextureEntry *entries = realloc(cache->entries,
                                          capacity * sizeof(*entries));
        if (!entries) {
            free(copy);
            return NULL;
        }
        cache->entries = entries;
        cache->entry_capacity = capacity;
    }

    memmove(cache->entries + index + 1, cache->entries + index,
            (cache->entry_count - index) * sizeof(*cache->entries));
    WmTextureEntry *entry = cache->entries + index;
    *entry = (WmTextureEntry){ .url = copy };
    cache->entry_count++;
    return entry;
}

static bool path_within_root(const WmTextureCache *cache, const char *path)
{
    size_t root_length = strlen(cache->raw_root);
    if (root_length == 1 && cache->raw_root[0] == '/') {
        return path[0] == '/' && path[1] != '\0';
    }
    return strncmp(path, cache->raw_root, root_length) == 0 &&
           path[root_length] == '/';
}

static char *resolved_raw_path(const WmTextureCache *cache, const char *url,
                               size_t url_length)
{
    size_t root_length = strlen(cache->raw_root);
    if (root_length > SIZE_MAX - url_length - 3) {
        return NULL;
    }
    char *candidate = malloc(root_length + url_length + 3);
    if (!candidate) {
        return NULL;
    }
    memcpy(candidate, cache->raw_root, root_length);
    candidate[root_length] = '/';
    memcpy(candidate + root_length + 1, url, url_length - 4);
    memcpy(candidate + root_length + 1 + url_length - 4, ".wmra", 6);

    char *canonical = realpath(candidate, NULL);
    free(candidate);
    if (!canonical) {
        return NULL;
    }
    if (!path_within_root(cache, canonical)) {
        free(canonical);
        return NULL;
    }
    return canonical;
}

static uint32_t read_little_endian_u32(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

/* Check the declared size before decoding, so an image above the GPU budget
 * never creates a large temporary pixel allocation. wm_image_read still checks
 * the complete versioned file, including its exact payload length. */
static bool declared_image_bytes(const char *path, size_t *bytes)
{
    FILE *file = fopen(path, "rb");
    if (!file) {
        return false;
    }
    uint8_t header[16];
    bool valid = fread(header, 1, sizeof(header), file) == sizeof(header);
    fclose(file);
    if (!valid || memcmp(header, "WMRA", 4) != 0 ||
        read_little_endian_u32(header + 4) != 1) {
        return false;
    }
    uint32_t width = read_little_endian_u32(header + 8);
    uint32_t height = read_little_endian_u32(header + 12);
    if (width == 0 || height == 0 || width > 8192 || height > 8192) {
        return false;
    }
    uint64_t count = (uint64_t)width * height * 4;
    if (count > SIZE_MAX || count > 256ULL * 1024 * 1024) {
        return false;
    }
    *bytes = (size_t)count;
    return true;
}

static WmTextureEntry *least_recent_eviction_candidate(WmTextureCache *cache)
{
    WmTextureEntry *candidate = NULL;
    for (size_t index = 0; index < cache->entry_count; ++index) {
        WmTextureEntry *entry = cache->entries + index;
        if (entry->state != WM_TEXTURE_RESIDENT || entry->frame_used == cache->frame) {
            continue;
        }
        if (!candidate || entry->recent_use < candidate->recent_use) {
            candidate = entry;
        }
    }
    return candidate;
}

static bool reserve_gpu_bytes(WmTextureCache *cache, size_t bytes)
{
    if (bytes > cache->budget_bytes) {
        return false;
    }
    while (cache->resident_bytes > cache->budget_bytes - bytes) {
        WmTextureEntry *victim = least_recent_eviction_candidate(cache);
        if (!victim) {
            return false;
        }
        wm_platform_destroy_texture(cache->platform, victim->handle);
        cache->resident_bytes -= victim->bytes;
        victim->handle = 0;
        victim->state = WM_TEXTURE_UNLOADED;
        cache->evictions++;
    }
    return true;
}

WmTextureCache *wm_texture_cache_create(WmPlatform *platform,
                                         const char *raw_root,
                                         size_t budget_bytes)
{
    if (!platform || !raw_root || !raw_root[0] || budget_bytes == 0) {
        return NULL;
    }
    char *canonical = realpath(raw_root, NULL);
    if (!canonical) {
        return NULL;
    }
    struct stat information;
    if (stat(canonical, &information) != 0 || !S_ISDIR(information.st_mode)) {
        free(canonical);
        return NULL;
    }
    WmTextureCache *cache = calloc(1, sizeof(*cache));
    if (!cache) {
        free(canonical);
        return NULL;
    }
    cache->platform = platform;
    cache->raw_root = canonical;
    cache->budget_bytes = budget_bytes;
    cache->frame = 1;
    return cache;
}

void wm_texture_cache_destroy(WmTextureCache *cache)
{
    if (!cache) {
        return;
    }
    for (size_t index = 0; index < cache->entry_count; ++index) {
        WmTextureEntry *entry = cache->entries + index;
        if (entry->state == WM_TEXTURE_RESIDENT) {
            wm_platform_destroy_texture(cache->platform, entry->handle);
        }
        free(entry->url);
    }
    free(cache->entries);
    free(cache->raw_root);
    free(cache);
}

void wm_texture_cache_begin_frame(WmTextureCache *cache)
{
    if (!cache) {
        return;
    }
    if (cache->frame == UINT64_MAX) {
        for (size_t index = 0; index < cache->entry_count; ++index) {
            cache->entries[index].frame_used = 0;
        }
        cache->frame = 1;
    } else {
        cache->frame++;
    }
}

static void record_use(WmTextureCache *cache, WmTextureEntry *entry)
{
    if (cache->use_clock == UINT64_MAX) {
        for (size_t index = 0; index < cache->entry_count; ++index) {
            cache->entries[index].recent_use = 0;
        }
        cache->use_clock = 0;
    }
    entry->frame_used = cache->frame;
    entry->recent_use = ++cache->use_clock;
}

bool wm_texture_cache_resolve(WmTextureCache *cache,
                              const char *relative_png_url,
                              uint32_t *handle)
{
    if (handle) {
        *handle = 0;
    }
    if (!cache || !handle) {
        return false;
    }
    size_t url_length;
    if (!valid_relative_png_url(relative_png_url, &url_length)) {
        return false;
    }

    size_t insertion_index;
    WmTextureEntry *entry = find_entry(cache, relative_png_url, &insertion_index);
    if (!entry) {
        entry = insert_entry(cache, relative_png_url, url_length, insertion_index);
        if (!entry) {
            return false;
        }
    }
    if (entry->state == WM_TEXTURE_RESIDENT) {
        record_use(cache, entry);
        *handle = entry->handle;
        return true;
    }
    if (entry->state == WM_TEXTURE_FAILED) {
        return false;
    }
    if (entry->bytes != 0 && !reserve_gpu_bytes(cache, entry->bytes)) {
        return false;
    }

    char *path = resolved_raw_path(cache, relative_png_url, url_length);
    if (!path) {
        entry->state = WM_TEXTURE_FAILED;
        return false;
    }
    size_t declared_bytes;
    if (!declared_image_bytes(path, &declared_bytes)) {
        free(path);
        entry->state = WM_TEXTURE_FAILED;
        return false;
    }
    entry->bytes = declared_bytes;
    if (declared_bytes > cache->budget_bytes) {
        free(path);
        entry->state = WM_TEXTURE_FAILED;
        return false;
    }
    if (!reserve_gpu_bytes(cache, declared_bytes)) {
        free(path);
        return false;
    }
    WmImage image;
    bool read_successful = wm_image_read(path, &image);
    free(path);
    if (!read_successful) {
        entry->state = WM_TEXTURE_FAILED;
        return false;
    }

    size_t bytes = (size_t)image.width * image.height * 4;
    entry->bytes = bytes;
    if (bytes != declared_bytes || bytes > cache->budget_bytes) {
        entry->state = WM_TEXTURE_FAILED;
        wm_image_free(&image);
        return false;
    }
    if (!reserve_gpu_bytes(cache, bytes)) {
        wm_image_free(&image);
        return false;
    }

    uint32_t texture = wm_platform_create_texture(cache->platform,
                                                    (int)image.width,
                                                    (int)image.height,
                                                    image.pixels);
    wm_image_free(&image);
    if (texture == 0) {
        entry->state = WM_TEXTURE_FAILED;
        return false;
    }
    entry->state = WM_TEXTURE_RESIDENT;
    entry->handle = texture;
    cache->resident_bytes += bytes;
    record_use(cache, entry);
    *handle = texture;
    return true;
}

bool wm_texture_cache_layout_image(void *context,
                                   const WmLayoutTexture *resource,
                                   uint32_t *handle)
{
    if (handle) {
        *handle = 0;
    }
    if (!resource || resource->missing) {
        return false;
    }
    return wm_texture_cache_resolve(context, resource->url, handle);
}

void wm_texture_cache_retry_failed(WmTextureCache *cache)
{
    if (!cache) {
        return;
    }
    for (size_t index = 0; index < cache->entry_count; ++index) {
        WmTextureEntry *entry = cache->entries + index;
        if (entry->state == WM_TEXTURE_FAILED) {
            entry->state = WM_TEXTURE_UNLOADED;
            entry->bytes = 0;
        }
    }
}

WmTextureCacheStats wm_texture_cache_stats(const WmTextureCache *cache)
{
    WmTextureCacheStats statistics = { 0 };
    if (!cache) {
        return statistics;
    }
    statistics.budget_bytes = cache->budget_bytes;
    statistics.resident_bytes = cache->resident_bytes;
    statistics.known_sources = cache->entry_count;
    statistics.evictions = cache->evictions;
    for (size_t index = 0; index < cache->entry_count; ++index) {
        WmTextureState state = cache->entries[index].state;
        if (state == WM_TEXTURE_RESIDENT) {
            statistics.resident_textures++;
        } else if (state == WM_TEXTURE_FAILED) {
            statistics.failed_sources++;
        }
    }
    return statistics;
}
