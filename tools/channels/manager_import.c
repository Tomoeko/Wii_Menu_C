#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "manager_import.h"
#include "manager_package.h"

#include "preparation/prepare_fs.h"
#include "wii_menu/support/json.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

static bool run_tool(const char *executable, const char *working_directory,
                     char *const arguments[]) {
    pid_t child = fork();
    if (child < 0)
        return false;
    if (child == 0) {
        if (chdir(working_directory) != 0)
            _exit(127);
        execv(executable, arguments);
        _exit(127);
    }
    int status;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR)
            return false;
    }
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static bool baseline_contains(const char *assets, const char *id) {
    char path[PREPARE_PATH_CAPACITY];
    if (!path_join(path, sizeof(path), assets, "channels.json"))
        return true;
    WmJson catalog;
    if (!wm_json_load(&catalog, path, 8 * 1024 * 1024))
        return true;
    bool found = false;
    size_t channels = wm_json_member(&catalog, 0, "channels");
    if (channels < catalog.count && catalog.tokens[channels].type == WM_JSON_ARRAY) {
        for (size_t index = 0; index < catalog.tokens[channels].children; index++) {
            size_t entry = wm_json_index(&catalog, channels, index);
            if (wm_json_equals(&catalog, wm_json_member(&catalog, entry, "id"), id)) {
                found = true;
            }
        }
    }
    wm_json_free(&catalog);
    return found;
}

static bool exported_channel(const char *stage, char id[65], char title[128]) {
    char path[PREPARE_PATH_CAPACITY];
    if (!path_join(path, sizeof(path), stage, "export/channels.json"))
        return false;
    WmJson catalog;
    if (!wm_json_load(&catalog, path, 8 * 1024 * 1024))
        return false;
    size_t channels = wm_json_member(&catalog, 0, "channels");
    bool valid = channels < catalog.count &&
                 catalog.tokens[channels].type == WM_JSON_ARRAY &&
                 catalog.tokens[channels].children == 1;
    if (valid) {
        size_t entry = wm_json_index(&catalog, channels, 0);
        char icon[256], banner[256], expected_icon[256], expected_banner[256];
        valid = wm_json_copy(&catalog, wm_json_member(&catalog, entry, "id"), id, 65) &&
                wm_local_native_id_valid(id) &&
                wm_json_copy_text(&catalog, wm_json_member(&catalog, entry, "title"),
                                  title, 128) &&
                title[0] &&
                wm_json_copy(&catalog, wm_json_member(&catalog, entry, "iconLayout"),
                             icon, sizeof(icon)) &&
                wm_json_copy(&catalog, wm_json_member(&catalog, entry, "bannerLayout"),
                             banner, sizeof(banner));
        if (valid) {
            snprintf(expected_icon, sizeof(expected_icon),
                     "channel-layouts/%s/icon/icon.json", id);
            snprintf(expected_banner, sizeof(expected_banner),
                     "channel-layouts/%s/banner/banner.json", id);
            valid = strcmp(icon, expected_icon) == 0 &&
                    strcmp(banner, expected_banner) == 0;
        }
    }
    wm_json_free(&catalog);
    return valid;
}

static bool arrange_extracted_title(const char *stage,
                                    char nand[PREPARE_PATH_CAPACITY]) {
    char extracted[PREPARE_PATH_CAPACITY];
    if (!path_join(extracted, sizeof(extracted), stage, ".local/wad") ||
        !path_join(nand, PREPARE_PATH_CAPACITY, stage, "nand"))
        return false;
    DIR *listing = opendir(extracted);
    if (!listing)
        return false;
    char title_id[17] = {0};
    bool valid = true;
    struct dirent *entry;
    while ((entry = readdir(listing)) != NULL) {
        if (entry->d_name[0] == '.')
            continue;
        if (!wm_local_native_id_valid(entry->d_name) || title_id[0]) {
            valid = false;
            break;
        }
        strcpy(title_id, entry->d_name);
    }
    if (closedir(listing) != 0)
        valid = false;
    if (!valid || !title_id[0])
        return false;

    char relative[128], source[PREPARE_PATH_CAPACITY];
    char destination[PREPARE_PATH_CAPACITY];
    int length = snprintf(relative, sizeof(relative), "%s/content", title_id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !path_join(source, sizeof(source), extracted, relative))
        return false;
    length = snprintf(relative, sizeof(relative), "title/%.8s/%.8s/content", title_id,
                      title_id + 8);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !path_join(destination, sizeof(destination), nand, relative) ||
        !make_file_parent(destination))
        return false;
    return rename(source, destination) == 0;
}

bool wm_channels_import_wad(const char *program, const char *assets, const char *wad,
                            const char *key_file, const char *key_index,
                            WmLocalCatalog *local) {
    int space = wm_channels_open_slot_status(assets);
    if (space != 1) {
        fputs(space < 0
                  ? "Prepared channel catalog is invalid.\n"
                  : "All 48 channel slots are occupied. Remove a channel first.\n",
              stderr);
        return false;
    }
    char *asset_root = realpath(assets, NULL);
    char *wad_path = realpath(wad, NULL);
    char *key_path = key_file ? realpath(key_file, NULL) : NULL;
    char extractor[PREPARE_PATH_CAPACITY], exporter[PREPARE_PATH_CAPACITY];
    bool valid = asset_root && wad_path && (!key_file || key_path) &&
                 wm_channels_regular_file(wad_path) &&
                 (!key_path || wm_channels_regular_file(key_path)) &&
                 wm_channels_sibling_tool(program, "wm-wad-extract", extractor) &&
                 wm_channels_sibling_tool(program, "wm-channel-export", exporter);
    if (!valid) {
        fputs("WAD, key, assets, or sibling extraction tools are unavailable.\n",
              stderr);
        free(asset_root);
        free(wad_path);
        free(key_path);
        return false;
    }

    char stage[PREPARE_PATH_CAPACITY];
    /* Keep extraction private until its catalog and files have been validated. */
    valid = path_join(stage, sizeof(stage), asset_root, ".channel-import.XXXXXX") &&
            mkdtemp(stage) != NULL;
    if (!valid) {
        free(asset_root);
        free(wad_path);
        free(key_path);
        return false;
    }
    char *extract_args[9] = {extractor, "--wad", wad_path};
    size_t argument = 3;
    if (key_path) {
        extract_args[argument++] = "--common-key-file";
        extract_args[argument++] = key_path;
    }
    if (key_index) {
        extract_args[argument++] = "--common-key-index";
        extract_args[argument++] = (char *)key_index;
    }
    extract_args[argument] = NULL;
    valid = run_tool(extractor, stage, extract_args);

    char nand[PREPARE_PATH_CAPACITY], export_root[PREPARE_PATH_CAPACITY];
    valid = valid && arrange_extracted_title(stage, nand) &&
            path_join(export_root, sizeof(export_root), stage, "export");
    char *export_args[] = {exporter, nand, export_root, "ENG", NULL};
    if (valid)
        valid = run_tool(exporter, stage, export_args);

    char id[65] = {0}, title[128] = {0};
    if (valid)
        valid =
            exported_channel(stage, id, title) && !baseline_contains(asset_root, id);
    size_t existing = 0;
    while (valid && existing < local->channel_count &&
           strcmp(local->channels[existing].id, id) != 0)
        existing++;
    if (valid && existing < local->channel_count) {
        WmLocalChannel *channel = &local->channels[existing];
        valid = channel->imported && channel->removed &&
                strcmp(channel->title, title) == 0 &&
                wm_channels_installed_ready(asset_root, id, true);
        if (valid) {
            channel->removed = false;
            valid = wm_local_catalog_save(asset_root, local);
        }
    } else if (valid) {
        char source[PREPARE_PATH_CAPACITY], destination[PREPARE_PATH_CAPACITY];
        char relative[128], audio_source[PREPARE_PATH_CAPACITY];
        char audio_destination[PREPARE_PATH_CAPACITY];
        int path_length =
            snprintf(relative, sizeof(relative), "channel-layouts/%s", id);
        valid = local->channel_count < WM_LOCAL_CHANNEL_LIMIT && path_length > 0 &&
                (size_t)path_length < sizeof(relative) &&
                path_join(source, sizeof(source), export_root, relative) &&
                path_join(destination, sizeof(destination), asset_root, relative) &&
                path_join(audio_source, sizeof(audio_source), export_root,
                          "channel-audio") &&
                path_join(audio_destination, sizeof(audio_destination), asset_root,
                          "channel-audio");
        char audio_name[96];
        snprintf(audio_name, sizeof(audio_name), "%s.wav", id);
        char source_audio[PREPARE_PATH_CAPACITY], target_audio[PREPARE_PATH_CAPACITY];
        if (valid)
            valid = path_join(source_audio, sizeof(source_audio), audio_source,
                              audio_name) &&
                    path_join(target_audio, sizeof(target_audio), audio_destination,
                              audio_name);
        bool has_audio = valid && wm_channels_regular_file(source_audio);
        struct stat metadata;
        if (valid)
            valid = lstat(destination, &metadata) != 0 && errno == ENOENT;
        if (valid && has_audio)
            valid = lstat(target_audio, &metadata) != 0 && errno == ENOENT;
        char layout_root[PREPARE_PATH_CAPACITY];
        bool destination_created = false;
        if (valid) {
            valid = path_join(layout_root, sizeof(layout_root), asset_root,
                              "channel-layouts") &&
                    (mkdir(layout_root, 0700) == 0 || errno == EEXIST) &&
                    copy_tree(source, destination);
            destination_created = lstat(destination, &metadata) == 0;
        }
        bool audio_created = false;
        if (valid && has_audio) {
            valid = (mkdir(audio_destination, 0700) == 0 || errno == EEXIST) &&
                    copy_file(source_audio, target_audio);
            audio_created = valid;
        }
        if (valid) {
            WmLocalChannel *channel = &local->channels[local->channel_count++];
            strcpy(channel->id, id);
            strcpy(channel->title, title);
            channel->imported = true;
            channel->removed = false;
            valid = wm_local_catalog_save(asset_root, local);
        }
        if (!valid) {
            if (destination_created)
                remove_tree(destination);
            if (audio_created)
                unlink(target_audio);
        }
    }
    if (!remove_tree(stage)) {
        fprintf(stderr, "Private import staging needs cleanup: %s\n", stage);
    }
    if (valid)
        printf("Installed channel %s (%s). Restart the menu to see it.\n", id, title);
    else
        fputs("Channel WAD import failed; prepared files were preserved.\n", stderr);
    free(asset_root);
    free(wad_path);
    free(key_path);
    return valid;
}
