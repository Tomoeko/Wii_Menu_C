#include "export_internal.h"
#include "wii_menu/resources/resource_chans.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool wm_export_language_script(const uint8_t *data, size_t size, const char *output,
                               const char *channel_id, bool *found) {
    if (size < 4 || memcmp(data, "\x55\xaa\x38\x2d", 4))
        return true;
    WmU8Archive archive = {0};
    char error[160];
    if (!wm_u8_parse(data, size, &archive, error, sizeof(error)))
        return false;
    const WmU8Entry *entry = wm_u8_find(&archive, "arc/icon.cs.lz7");
    if (!entry)
        entry = wm_u8_find(&archive, "arc/icon.cs");
    bool valid = true;
    uint8_t *decoded = NULL;
    size_t length = 0;
    WmChansLanguages *languages = NULL;
    if (!entry)
        goto release_archive;
    if (*found || !wm_unwrap_resource(entry->data, entry->size, &decoded, &length)) {
        valid = false;
        goto release_archive;
    }
    languages = malloc(sizeof(*languages));
    if (!languages) {
        valid = false;
        goto release_archive;
    }
    if (!wm_chans_languages_decode(decoded, length, languages, error, sizeof(error))) {
        fprintf(stderr, "Channel %s: unsupported icon language script; %s.\n",
                channel_id, error);
        goto release_archive;
    }
    char destination[WM_PATH_CAP];
    int written =
        snprintf(destination, sizeof(destination),
                 "%s/channel-layouts/%s/icon/icon.json.cs", output, channel_id);
    valid = written > 0 && (size_t)written < sizeof(destination) &&
            wm_output_write_file(destination, decoded, length);
    if (valid) {
        *found = true;
        printf("Recovered %zu channel language text bindings for %s.\n",
               languages->count, channel_id);
    }
release_archive:
    free(languages);
    free(decoded);
    wm_u8_free(&archive);
    return valid;
}
