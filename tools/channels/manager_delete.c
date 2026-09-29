#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "manager_delete.h"
#include "manager_package.h"
#include "preparation/prepare_fs.h"
#include "wii_menu/support/json.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { WM_PURGE_MAX_OWNED_FILES = 35, WM_PURGE_FILENAME_CAPACITY = 128 };

static bool has_suffix(const char *name, const char *suffix) {
    size_t name_length = strlen(name);
    size_t suffix_length = strlen(suffix);
    return name_length > suffix_length &&
           strcmp(name + name_length - suffix_length, suffix) == 0;
}

static bool imported_textures_safe(const char *path) {
    DIR *listing = opendir(path);
    if (!listing)
        return false;
    bool safe = true;
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(listing)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char file[4096];
        if (++count > 2048 || !has_suffix(entry->d_name, ".wmra") ||
            !wm_channels_join(file, path, entry->d_name) ||
            !wm_channels_regular_file(file))
            safe = false;
    }
    if (closedir(listing) != 0)
        safe = false;
    return safe;
}

static bool imported_layout_safe(const char *path, const char *kind) {
    DIR *listing = opendir(path);
    if (!listing)
        return false;
    char layout_name[32];
    int length = snprintf(layout_name, sizeof(layout_name), "%s.json", kind);
    bool safe = length > 0 && (size_t)length < sizeof(layout_name);
    bool has_layout = false;
    struct dirent *entry;
    while ((entry = readdir(listing)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[4096];
        if (!wm_channels_join(child, path, entry->d_name)) {
            safe = false;
        } else if (strcmp(entry->d_name, layout_name) == 0) {
            has_layout = wm_channels_regular_file(child);
            if (!has_layout)
                safe = false;
        } else if (strcmp(entry->d_name, "textures") == 0) {
            if (!wm_channels_directory(child) || !imported_textures_safe(child))
                safe = false;
        } else {
            safe = false;
        }
    }
    if (closedir(listing) != 0)
        safe = false;
    return safe && has_layout;
}

static bool imported_tree_safe(const char *path) {
    DIR *listing = opendir(path);
    if (!listing)
        return false;
    bool safe = true;
    bool has_icon = false;
    bool has_banner = false;
    struct dirent *entry;
    while ((entry = readdir(listing)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        char child[4096];
        if (!wm_channels_join(child, path, entry->d_name) ||
            !wm_channels_directory(child)) {
            safe = false;
        } else if (strcmp(entry->d_name, "icon") == 0) {
            has_icon = imported_layout_safe(child, "icon");
            if (!has_icon)
                safe = false;
        } else if (strcmp(entry->d_name, "banner") == 0) {
            has_banner = imported_layout_safe(child, "banner");
            if (!has_banner)
                safe = false;
        } else {
            safe = false;
        }
    }
    if (closedir(listing) != 0)
        safe = false;
    return safe && has_icon && has_banner;
}

static bool purge_imported_files(const char *assets, const char *id) {
    /* Refuse a modified export tree before removing any managed content. */
    char parent[4096], path[4096], audio_parent[4096], audio[4096];
    char relative[128];
    int length = snprintf(relative, sizeof(relative), "channel-layouts/%s", id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !wm_channels_join(parent, assets, "channel-layouts") ||
        !wm_channels_directory(parent) || !wm_channels_join(path, assets, relative) ||
        !wm_channels_directory(path) || !regular_tree(path) ||
        !imported_tree_safe(path) ||
        !wm_channels_join(audio_parent, assets, "channel-audio"))
        return false;
    length = snprintf(relative, sizeof(relative), "%s.wav", id);
    if (length < 0 || (size_t)length >= sizeof(relative) ||
        !wm_channels_join(audio, audio_parent, relative))
        return false;
    struct stat metadata;
    bool has_audio = lstat(audio, &metadata) == 0;
    if (has_audio &&
        (!wm_channels_directory(audio_parent) || !S_ISREG(metadata.st_mode)))
        return false;
    if (!remove_tree(path))
        return false;
    return !has_audio || unlink(audio) == 0;
}

static bool collect_owned_texture_files(
    const char *path, const char *id,
    char owned[WM_PURGE_MAX_OWNED_FILES][WM_PURGE_FILENAME_CAPACITY],
    size_t *owned_count) {
    static const char *const layouts[] = {"icon.json", "banner.json"};
    for (size_t layout_index = 0; layout_index < 2; layout_index++) {
        char source[4096];
        WmJson json;
        if (!wm_channels_join(source, path, layouts[layout_index]) ||
            !wm_json_load(&json, source, 2 * 1024 * 1024))
            return false;

        size_t textures = wm_json_member(&json, 0, "textures");
        bool okay = textures < json.count &&
                    json.tokens[textures].type == WM_JSON_ARRAY &&
                    json.tokens[textures].children <= 16;
        for (size_t texture = 0; okay && texture < json.tokens[textures].children;
             texture++) {
            size_t entry = wm_json_index(&json, textures, texture);
            char url[256], prefix[128];
            int length = snprintf(prefix, sizeof(prefix), "custom-channels/%s/", id);
            okay = length > 0 && (size_t)length < sizeof(prefix) &&
                   wm_json_copy(&json, wm_json_member(&json, entry, "url"), url,
                                sizeof(url)) &&
                   strncmp(url, prefix, strlen(prefix)) == 0 &&
                   wm_channels_texture_filename(url + strlen(prefix));
            if (!okay)
                break;

            /* The filename validator limits names to 120 bytes. Replacing
             * .png with .wmra adds one byte and still fits this buffer. */
            char filename[WM_PURGE_FILENAME_CAPACITY];
            strcpy(filename, url + strlen(prefix));
            strcpy(filename + strlen(filename) - 4, ".wmra");
            bool duplicate = false;
            for (size_t previous = 0; previous < *owned_count; previous++) {
                if (strcmp(owned[previous], filename) == 0)
                    duplicate = true;
            }
            if (!duplicate) {
                okay = *owned_count < WM_PURGE_MAX_OWNED_FILES;
                if (okay)
                    strcpy(owned[(*owned_count)++], filename);
            }
        }
        wm_json_free(&json);
        if (!okay)
            return false;
    }
    return true;
}

static bool
owned_directory_safe(int root,
                     char owned[WM_PURGE_MAX_OWNED_FILES][WM_PURGE_FILENAME_CAPACITY],
                     size_t owned_count) {
    int listing_fd = dup(root);
    if (listing_fd < 0)
        return false;
    DIR *listing = fdopendir(listing_fd);
    if (!listing) {
        close(listing_fd);
        return false;
    }
    bool okay = true;
    struct dirent *entry;
    while ((entry = readdir(listing)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        bool known = false;
        for (size_t item = 0; item < owned_count; item++) {
            if (strcmp(entry->d_name, owned[item]) == 0)
                known = true;
        }
        struct stat metadata;
        if (!known ||
            fstatat(root, entry->d_name, &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISREG(metadata.st_mode))
            okay = false;
    }
    if (closedir(listing) != 0)
        okay = false;
    for (size_t item = 0; item < owned_count; item++) {
        struct stat metadata;
        if (fstatat(root, owned[item], &metadata, AT_SYMLINK_NOFOLLOW) != 0 ||
            !S_ISREG(metadata.st_mode))
            okay = false;
    }
    return okay;
}

int wm_channels_purge(const char *assets, WmLocalCatalog *local, const char *id) {
    size_t index = 0;
    while (index < local->channel_count && strcmp(local->channels[index].id, id) != 0)
        index++;
    if (index == local->channel_count || !local->channels[index].removed) {
        fputs("Remove the custom channel before purging it.\n", stderr);
        return 1;
    }
    if (local->channels[index].imported) {
        if (!purge_imported_files(assets, id)) {
            fputs("Imported channel files are unsafe or incomplete; nothing "
                  "was removed from the catalog.\n",
                  stderr);
            return 1;
        }
        memmove(&local->channels[index], &local->channels[index + 1],
                (local->channel_count - index - 1) * sizeof(local->channels[0]));
        local->channel_count--;
        if (!wm_local_catalog_save(assets, local))
            return 1;
        printf("Permanently deleted managed files for %s. Source WAD was kept.\n", id);
        return 0;
    }
    char path[4096];
    if (!wm_channels_package_directory(path, assets, id) ||
        !wm_channels_directory(path))
        return 1;
    int root = open(path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW);
    if (root < 0)
        return 1;
    WmChannelPackage package;
    bool okay = wm_channels_package_read(path, &package) && strcmp(package.id, id) == 0;
    char owned[WM_PURGE_MAX_OWNED_FILES][WM_PURGE_FILENAME_CAPACITY] = {
        "channel.json", "icon.json", "banner.json"};
    size_t owned_count = 3;
    if (okay)
        okay = collect_owned_texture_files(path, id, owned, &owned_count);
    if (okay)
        okay = owned_directory_safe(root, owned, owned_count);
    char audio_path[4096], relative[128];
    if (okay && package.has_audio) {
        int length = snprintf(relative, sizeof(relative), "channel-audio/%s.wav", id);
        struct stat metadata;
        okay = length > 0 && (size_t)length < sizeof(relative) &&
               wm_channels_join(audio_path, assets, relative) &&
               lstat(audio_path, &metadata) == 0 && S_ISREG(metadata.st_mode);
    }
    if (okay) {
        for (size_t item = 0; item < owned_count; item++) {
            if (unlinkat(root, owned[item], 0) != 0)
                okay = false;
        }
    }
    close(root);
    if (okay && rmdir(path) != 0)
        okay = false;
    if (okay && package.has_audio && unlink(audio_path) != 0)
        okay = false;
    if (!okay) {
        fputs("Purge stopped: channel directory has missing or unowned files.\n",
              stderr);
        return 1;
    }
    memmove(&local->channels[index], &local->channels[index + 1],
            (local->channel_count - index - 1) * sizeof(local->channels[0]));
    local->channel_count--;
    if (!wm_local_catalog_save(assets, local))
        return 1;
    printf("Permanently deleted managed files for %s. Source folder was kept.\n", id);
    return 0;
}
