#ifndef WM_CHANNEL_EXPORT_INTERNAL_H
#define WM_CHANNEL_EXPORT_INTERNAL_H

#include "wii_menu/persistence/saved_layout.h"
#include "wii_menu/resources/resource_u8.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WM_PATH_CAP = 4096, WM_MAX_CONTENT = 64 * 1024 * 1024 };

static inline uint16_t wm_be16(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | bytes[1]);
}

static inline uint32_t wm_be32(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static inline bool wm_fits(size_t size, size_t offset, size_t length) {
    return offset <= size && length <= size - offset;
}

typedef struct WmChannelExport {
    char id[17];
    char short_id[5];
    char title[512];
    char titles[10][512];
    char content_id[9];
    char source_file[128];
    char content_sha1[41];
    char tmd_sha1[41];
    char icon_layout[WM_PATH_CAP];
    char banner_layout[WM_PATH_CAP];
    struct WmLayoutPath *layouts[2];
    size_t layout_count[2];
    unsigned version;
    unsigned tmd_version;
    uint32_t flags;
    unsigned icon_textures;
    unsigned banner_textures;
    unsigned icon_animations;
    unsigned banner_animations;
    bool preferred;
    bool has_audio;
    uint32_t audio_rate;
    uint32_t audio_frames;
    uint32_t audio_loop_start;
    uint32_t audio_loop_end;
    uint8_t audio_channels;
    bool audio_looping;
} WmChannelExport;

typedef struct WmLayoutPath {
    char name[128];
    char path[WM_PATH_CAP];
} WmLayoutPath;

typedef struct WmChannelList {
    WmChannelExport *items;
    size_t count;
    size_t capacity;
} WmChannelList;

extern const char *const wm_languages[10];

bool wm_output_parent(const char *path);
bool wm_output_target_safe(const char *path);
bool wm_output_write_file(const char *path, const void *data, size_t size);
bool wm_write_manifest(const char *output, const char *language,
                       WmChannelList *channels, const WmSavedLayout *saved_layout);
bool wm_export_channel_audio(const WmU8Entry *entry, const char *output,
                             WmChannelExport *channel);
bool wm_export_resource(const WmU8Entry *entry, const char *output,
                        const char *channel_id, const char *kind,
                        const char *source_file, WmLayoutPath **layout_paths,
                        size_t *layout_count, char default_layout[WM_PATH_CAP],
                        unsigned *texture_count, unsigned *animation_count);

#endif
