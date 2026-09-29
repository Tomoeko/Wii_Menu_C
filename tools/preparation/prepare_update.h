#ifndef WM_PREPARATION_PREPARE_UPDATE_H
#define WM_PREPARATION_PREPARE_UPDATE_H

#include "wii_menu/support/json.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

enum { PREPARE_MAX_CHANNELS = 2048, PREPARE_MAX_MANIFEST = 8 * 1024 * 1024 };

typedef struct PrepareChoices {
    char ids[PREPARE_MAX_CHANNELS][17];
    size_t count;
} PrepareChoices;

typedef struct PrepareChannel {
    char id[17];
    char content_sha1[41];
    char tmd_sha1[41];
    size_t token;
    bool incoming;
} PrepareChannel;

typedef struct PrepareManifest {
    WmJson json;
    PrepareChannel *channels;
    size_t count;
    size_t default_order;
    size_t saved_layout;
    char language[4];
} PrepareManifest;

bool prepare_choice_contains(const PrepareChoices *choices, const char *id);
bool prepare_add_choice(PrepareChoices *choices, const char *value);
bool prepare_open_manifest(const char *directory, PrepareManifest *manifest);
void prepare_close_manifest(PrepareManifest *manifest);
bool prepare_hash_regular_file(const char *path, char hexadecimal[41]);
bool prepare_print_update_plan(FILE *stream, const char *base_assets,
                               const char *incoming_assets,
                               const char input_nand_sha1[41],
                               const PrepareChoices *replace_ids,
                               const PrepareChoices *keep_ids, bool replace_all);
bool prepare_verify_expected_plan(const char *expected_path, const char *base_assets,
                                  const char *incoming_assets,
                                  const char input_nand_sha1[41],
                                  const PrepareChoices *replace_ids,
                                  const PrepareChoices *keep_ids, bool replace_all);
bool prepare_update_channels(const char *base_assets, const char *incoming_assets,
                             const char *staged_assets,
                             const PrepareChoices *replace_ids,
                             const PrepareChoices *keep_ids, bool replace_all);

#endif
