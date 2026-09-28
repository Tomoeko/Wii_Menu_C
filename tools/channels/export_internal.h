#ifndef WM_CHANNEL_EXPORT_INTERNAL_H
#define WM_CHANNEL_EXPORT_INTERNAL_H

#include "wii_menu/persistence/saved_layout.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

enum { WM_PATH_CAP = 4096 };

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
bool wm_write_manifest(const char *output, const char *language,
                       WmChannelList *channels, const WmSavedLayout *saved_layout);

#endif
