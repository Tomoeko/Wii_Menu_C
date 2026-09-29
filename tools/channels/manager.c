#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "asset_path.h"
#include "manager_delete.h"
#include "manager_import.h"
#include "manager_package.h"
#include "wii_menu/menu/local_catalog.h"
#include "wii_menu/support/ascii.h"
#include "wii_menu/support/json.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int usage(const char *program, int result) {
    fprintf(stderr,
            "Usage: %s [--assets DIRECTORY] list\n"
            "       %s [--assets DIRECTORY] validate FOLDER\n"
            "       %s [--assets DIRECTORY] add FOLDER-OR-CHANNEL.wad\n"
            "       %s [--assets DIRECTORY] remove ID-OR-NAME-OR-FOLDER\n"
            "       %s [--assets DIRECTORY] restore ID-OR-NAME-OR-FOLDER\n"
            "       %s [--assets DIRECTORY] purge ID-OR-NAME-OR-FOLDER --yes\n"
            "       %s [--assets DIRECTORY] preview ID-OR-NAME-OR-FOLDER\n",
            program, program, program, program, program, program, program);
    fputs("Add installs an authored vector/PNG-layout folder. Remove hides a channel "
          "and keeps its files. Restore reverses removal. Purge permanently "
          "deletes managed installation files only. Names must identify one "
          "channel.\n"
          "The prepared WAD manifest stays unchanged. SVG guides are authoring "
          "references; export artwork as PNG or vector panes.\n"
          "WAD options: --common-key-file FILE --common-key-index N.\n",
          stderr);
    return result;
}

static bool require_open_slot(const char *assets) {
    int status = wm_channels_open_slot_status(assets);
    if (status < 0)
        fputs("Prepared channel catalog is invalid.\n", stderr);
    else if (status == 0)
        fputs("All 48 channel slots are occupied. Remove a channel first.\n", stderr);
    return status == 1;
}

static bool native_catalog(const char *assets, WmJson *json) {
    char path[4096];
    return wm_channels_join(path, assets, "channels.json") &&
           wm_json_load(json, path, 8 * 1024 * 1024);
}

static bool resolve_target(const char *assets, const WmLocalCatalog *local,
                           const char *target, char id[65], bool *custom) {
    WmChannelPackage package;
    bool folder = wm_channels_directory(target);
    if (folder) {
        if (!wm_channels_package_read(target, &package))
            return false;
        target = package.id;
    }
    for (size_t index = 0; index < local->channel_count; index++) {
        if (strcmp(local->channels[index].id, target) == 0) {
            strcpy(id, target);
            *custom = true;
            return true;
        }
    }
    if (folder) {
        fprintf(stderr, "Channel in folder is not installed: %s\n", target);
        return false;
    }
    WmJson native;
    if (!native_catalog(assets, &native))
        return false;
    size_t matches = 0;
    char candidate[65];
    bool managed = false;
    for (size_t index = 0; index < local->channel_count; index++) {
        const WmLocalChannel *channel = &local->channels[index];
        if (strcmp(channel->id, target) != 0 &&
            !wm_ascii_equal_ignore_case(channel->title, target))
            continue;
        strcpy(candidate, channel->id);
        matches++;
        managed = true;
    }
    size_t channels = wm_json_member(&native, 0, "channels");
    if (channels < native.count && native.tokens[channels].type == WM_JSON_ARRAY) {
        for (size_t index = 0; index < native.tokens[channels].children; index++) {
            size_t entry = wm_json_index(&native, channels, index);
            char native_id[65], title[128];
            if (!wm_json_copy(&native, wm_json_member(&native, entry, "id"), native_id,
                              sizeof(native_id)) ||
                !wm_json_copy_text(&native, wm_json_member(&native, entry, "title"),
                                   title, sizeof(title)))
                continue;
            if (strcmp(native_id, target) == 0) {
                strcpy(id, native_id);
                *custom = false;
                wm_json_free(&native);
                return true;
            }
            if (strcmp(native_id, target) != 0 &&
                !wm_ascii_equal_ignore_case(title, target))
                continue;
            strcpy(candidate, native_id);
            matches++;
            managed = false;
        }
    }
    wm_json_free(&native);
    if (matches != 1) {
        fprintf(stderr, "%s: %s channel match. Use the exact ID.\n", target,
                matches ? "ambiguous" : "no");
        return false;
    }
    strcpy(id, candidate);
    *custom = managed;
    return true;
}

static int command_list(const char *assets, const WmLocalCatalog *local) {
    WmJson native;
    if (!native_catalog(assets, &native))
        return 1;
    size_t channels = wm_json_member(&native, 0, "channels");
    if (channels < native.count && native.tokens[channels].type == WM_JSON_ARRAY) {
        for (size_t index = 0; index < native.tokens[channels].children; index++) {
            size_t entry = wm_json_index(&native, channels, index);
            char id[65], title[128];
            if (!wm_json_copy(&native, wm_json_member(&native, entry, "id"), id,
                              sizeof(id)) ||
                !wm_json_copy_text(&native, wm_json_member(&native, entry, "title"),
                                   title, sizeof(title)))
                continue;
            bool hidden = false;
            for (size_t item = 0; item < local->hidden_count; item++) {
                if (strcmp(local->hidden[item], id) == 0)
                    hidden = true;
            }
            printf("%s\t%s\t%s\n", id, hidden ? "removed" : "native", title);
        }
    }
    wm_json_free(&native);
    for (size_t index = 0; index < local->channel_count; index++) {
        const WmLocalChannel *channel = &local->channels[index];
        printf("%s\t%s\t%s\n", channel->id,
               channel->removed    ? "removed"
               : channel->imported ? "imported"
                                   : "custom",
               channel->title);
    }
    return 0;
}

static int command_add(const char *assets, WmLocalCatalog *local, const char *folder) {
    WmChannelPackage package;
    if (!wm_channels_package_read(folder, &package) ||
        !wm_channels_package_validate(&package)) {
        fputs("Invalid authored channel folder.\n", stderr);
        return 1;
    }
    if (!require_open_slot(assets)) {
        return 1;
    }
    for (size_t index = 0; index < local->channel_count; index++) {
        if (strcmp(local->channels[index].id, package.id) == 0) {
            if (!local->channels[index].removed) {
                fputs("Channel ID is already installed.\n", stderr);
                return 1;
            }
            char installed[4096];
            WmChannelPackage existing;
            if (!wm_channels_package_directory(installed, assets, package.id) ||
                !wm_channels_package_read(installed, &existing) ||
                strcmp(existing.title, package.title) != 0 ||
                !wm_channels_installed_ready(assets, package.id, false)) {
                fputs("Removed installation is incomplete or has a different title.\n",
                      stderr);
                return 1;
            }
            local->channels[index].removed = false;
            if (!wm_local_catalog_save(assets, local))
                return 1;
            printf("Restored %s. Existing installed files were reused.\n", package.id);
            return 0;
        }
    }
    if (local->channel_count == WM_LOCAL_CHANNEL_LIMIT ||
        !wm_channels_package_install(assets, &package)) {
        fputs("Could not install channel files; existing files were preserved.\n",
              stderr);
        return 1;
    }
    WmLocalChannel *channel = &local->channels[local->channel_count++];
    strcpy(channel->id, package.id);
    strcpy(channel->title, package.title);
    channel->removed = false;
    if (!wm_local_catalog_save(assets, local)) {
        fputs("Could not publish channel catalog. Installed files remain for "
              "inspection.\n",
              stderr);
        return 1;
    }
    printf("Installed %s. Restart the menu to see it.\n", package.id);
    return 0;
}

static int command_remove(const char *assets, WmLocalCatalog *local,
                          const char *const *targets, size_t count) {
    char ids[WM_LOCAL_CHANNEL_LIMIT][65];
    bool custom[WM_LOCAL_CHANNEL_LIMIT];
    if (count > WM_LOCAL_CHANNEL_LIMIT)
        return 2;
    for (size_t index = 0; index < count; index++) {
        if (!resolve_target(assets, local, targets[index], ids[index], &custom[index]))
            return 1;
        for (size_t earlier = 0; earlier < index; earlier++) {
            if (strcmp(ids[earlier], ids[index]) == 0) {
                fprintf(stderr, "Duplicate remove target: %s\n", ids[index]);
                return 2;
            }
        }
    }
    for (size_t index = 0; index < count; index++) {
        if (custom[index]) {
            for (size_t item = 0; item < local->channel_count; item++) {
                if (strcmp(local->channels[item].id, ids[index]) == 0)
                    local->channels[item].removed = true;
            }
        } else {
            bool already_hidden = false;
            for (size_t item = 0; item < local->hidden_count; item++) {
                if (strcmp(local->hidden[item], ids[index]) == 0)
                    already_hidden = true;
            }
            if (already_hidden)
                continue;
            if (local->hidden_count == WM_LOCAL_HIDDEN_LIMIT)
                return 1;
            strcpy(local->hidden[local->hidden_count++], ids[index]);
        }
    }
    if (!wm_local_catalog_save(assets, local))
        return 1;
    for (size_t index = 0; index < count; index++) {
        printf("Removed %s from the menu; source and installed files were kept.\n",
               ids[index]);
    }
    return 0;
}

static int command_restore(const char *assets, WmLocalCatalog *local,
                           const char *target) {
    char id[65];
    bool managed;
    if (!resolve_target(assets, local, target, id, &managed))
        return 1;
    if (managed) {
        for (size_t index = 0; index < local->channel_count; index++) {
            if (strcmp(local->channels[index].id, id) != 0)
                continue;
            if (!local->channels[index].removed)
                return 0;
            if (!require_open_slot(assets)) {
                return 1;
            }
            if (!wm_channels_installed_ready(assets, id,
                                             local->channels[index].imported)) {
                fputs("Installed channel files are missing; cannot restore.\n", stderr);
                return 1;
            }
            local->channels[index].removed = false;
            if (!wm_local_catalog_save(assets, local))
                return 1;
            printf("Restored %s.\n", id);
            return 0;
        }
        return 1;
    }
    for (size_t index = 0; index < local->hidden_count; index++) {
        if (strcmp(local->hidden[index], id) != 0)
            continue;
        if (!require_open_slot(assets)) {
            return 1;
        }
        memmove(&local->hidden[index], &local->hidden[index + 1],
                (local->hidden_count - index - 1) * sizeof(local->hidden[0]));
        local->hidden_count--;
        if (!wm_local_catalog_save(assets, local))
            return 1;
        printf("Restored %s.\n", id);
        return 0;
    }
    return 0;
}

static int command_purge(const char *assets, WmLocalCatalog *local,
                         const char *target) {
    char id[65];
    bool managed;
    if (!resolve_target(assets, local, target, id, &managed) || !managed) {
        fputs("Purge accepts only a managed channel.\n", stderr);
        return 1;
    }
    return wm_channels_purge(assets, local, id);
}

static int command_preview(const char *program, const char *assets,
                           const WmLocalCatalog *local, const char *target) {
    char id[65];
    bool custom;
    if (!resolve_target(assets, local, target, id, &custom))
        return 1;
    if (custom) {
        for (size_t index = 0; index < local->channel_count; index++) {
            if (strcmp(local->channels[index].id, id) == 0 &&
                local->channels[index].removed) {
                fputs("The channel is removed; add it before previewing.\n", stderr);
                return 1;
            }
        }
    } else {
        for (size_t index = 0; index < local->hidden_count; index++) {
            if (strcmp(local->hidden[index], id) == 0) {
                fputs("The channel is removed; restore it before previewing.\n",
                      stderr);
                return 1;
            }
        }
    }
    char sibling[4096];
#if defined(__APPLE__)
    const char *relative = "wii-menu.app/Contents/MacOS/wii-menu";
#else
    const char *relative = "wii-menu";
#endif
    if (!wm_channels_sibling_tool(program, relative, sibling)) {
        fputs("Build wii-menu beside wm-channels before previewing.\n", stderr);
        return 1;
    }
    char *const arguments[] = {sibling, "--assets", (char *)assets, "--preview-channel",
                               id,      NULL};
    execv(sibling, arguments);
    perror("Could not open channel preview");
    return 1;
}

static int lock_catalog(const char *assets) {
    char path[4096];
    if (!wm_channels_join(path, assets, ".channels.lock"))
        return -1;
    int descriptor = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
    struct stat metadata;
    if (descriptor < 0 || fstat(descriptor, &metadata) != 0 ||
        !S_ISREG(metadata.st_mode)) {
        if (descriptor >= 0)
            close(descriptor);
        return -1;
    }
    struct flock lock = {0};
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;
    while (fcntl(descriptor, F_SETLKW, &lock) != 0) {
        if (errno != EINTR) {
            close(descriptor);
            return -1;
        }
    }
    return descriptor;
}

static int run_command(const char *program, const char *command, const char *assets,
                       const char *const *targets, size_t target_count, bool confirmed,
                       const char *key_file, const char *key_index,
                       WmLocalCatalog *local) {
    if (strcmp(command, "list") == 0)
        return command_list(assets, local);
    if (!targets || target_count == 0 || !targets[0])
        return usage(program, 2);
    if (strcmp(command, "validate") == 0) {
        WmChannelPackage package;
        bool valid = wm_channels_package_read(targets[0], &package) &&
                     wm_channels_package_validate(&package);
        if (valid)
            printf("Valid custom channel: %s (%s)\n", package.id, package.title);
        return valid ? 0 : 1;
    }
    if (strcmp(command, "add") == 0) {
        if (wm_channels_regular_file(targets[0])) {
            return wm_channels_import_wad(program, assets, targets[0], key_file,
                                          key_index, local)
                       ? 0
                       : 1;
        }
        if (key_file || key_index)
            return usage(program, 2);
        return command_add(assets, local, targets[0]);
    }
    if (strcmp(command, "remove") == 0)
        return command_remove(assets, local, targets, target_count);
    if (strcmp(command, "restore") == 0)
        return command_restore(assets, local, targets[0]);
    if (strcmp(command, "purge") == 0) {
        if (!confirmed) {
            fputs("Purge permanently deletes managed files; pass --yes.\n", stderr);
            return 2;
        }
        return command_purge(assets, local, targets[0]);
    }
    if (strcmp(command, "preview") == 0)
        return command_preview(program, assets, local, targets[0]);
    return usage(program, 2);
}

int main(int argc, char **argv) {
    const char *assets = NULL;
    const char *key_file = NULL;
    const char *key_index = NULL;
    const char *command = NULL;
    const char *targets[WM_LOCAL_CHANNEL_LIMIT] = {0};
    size_t target_count = 0;
    bool confirmed = false;
    bool wad_option = false;
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], "--help") == 0)
            return usage(argv[0], 0);
        if (strcmp(argv[index], "--assets") == 0 && index + 1 < argc) {
            assets = argv[++index];
        } else if (strcmp(argv[index], "--common-key-file") == 0 && index + 1 < argc) {
            key_file = argv[++index];
        } else if (strcmp(argv[index], "--common-key-index") == 0 && index + 1 < argc) {
            key_index = argv[++index];
        } else if (strcmp(argv[index], "--wad") == 0 && index + 1 < argc) {
            if (target_count != 0)
                return usage(argv[0], 2);
            wad_option = true;
            targets[target_count++] = argv[++index];
        } else if (strcmp(argv[index], "--yes") == 0) {
            confirmed = true;
        } else if (!command) {
            command = argv[index];
        } else if (target_count < WM_LOCAL_CHANNEL_LIMIT) {
            targets[target_count++] = argv[index];
        } else {
            return usage(argv[0], 2);
        }
    }
    bool target_count_valid = command && target_count == 1;
    if (command && strcmp(command, "list") == 0)
        target_count_valid = target_count == 0;
    if (command && strcmp(command, "remove") == 0)
        target_count_valid = target_count > 0;
    if (!target_count_valid || (confirmed && strcmp(command, "purge") != 0) ||
        (wad_option && strcmp(command, "add") != 0) ||
        ((key_file || key_index) && strcmp(command, "add") != 0))
        return usage(argv[0], 2);
    if (key_index) {
        char *end;
        long parsed = strtol(key_index, &end, 10);
        if (end == key_index || *end || parsed < 0 || parsed > 255)
            return usage(argv[0], 2);
    }
    char discovered_assets[WM_APP_ASSET_PATH_CAPACITY];
    if (!assets && wm_app_find_default_assets(argv[0], discovered_assets,
                                              sizeof(discovered_assets))) {
        assets = discovered_assets;
    }
    if (!assets || !wm_channels_directory(assets)) {
        fprintf(stderr, "Prepared asset directory is unavailable: %s\n",
                assets ? assets : ".local/native-assets");
        return 1;
    }
    bool mutating = strcmp(command, "add") == 0 || strcmp(command, "remove") == 0 ||
                    strcmp(command, "restore") == 0 || strcmp(command, "purge") == 0;
    int lock = mutating ? lock_catalog(assets) : -1;
    if (mutating && lock < 0) {
        perror("Could not lock local channel catalog");
        return 1;
    }
    WmLocalCatalog local;
    if (!wm_local_catalog_load(assets, &local)) {
        fputs("Local channel catalog is invalid. No changes made.\n", stderr);
        if (lock >= 0)
            close(lock);
        return 1;
    }
    int result = run_command(argv[0], command, assets, targets, target_count, confirmed,
                             key_file, key_index, &local);
    if (lock >= 0)
        close(lock);
    return result;
}
