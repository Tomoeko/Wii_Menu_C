#define _POSIX_C_SOURCE 200809L
#define _DARWIN_C_SOURCE 1

#include "manager_package.h"
#include "png.h"
#include "asset_path.h"
#include "preparation/prepare_fs.h"
#include "wii_menu/audio/audio_wave.h"
#include "wii_menu/layout/layout_runtime.h"
#include "wii_menu/menu/local_catalog.h"
#include "wii_menu/menu/menu.h"
#include "wii_menu/render/image.h"
#include "wii_menu/support/json.h"

#include "../../src/support/regular_file.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

bool wm_channels_join(char output[4096], const char *root, const char *leaf) {
    return path_join(output, 4096, root, leaf);
}

bool wm_channels_regular_file(const char *path) {
    struct stat metadata;
    return lstat(path, &metadata) == 0 && S_ISREG(metadata.st_mode);
}

bool wm_channels_directory(const char *path) {
    struct stat metadata;
    return lstat(path, &metadata) == 0 && S_ISDIR(metadata.st_mode);
}

bool wm_channels_sibling_tool(const char *program, const char *relative,
                              char output[4096]) {
    char *executable = wm_app_resolve_executable(program);
    if (!executable)
        return false;
    char *separator = strrchr(executable, '/');
    bool valid = false;
    if (separator) {
        *separator = '\0';
        valid = wm_channels_join(output, executable, relative) &&
                wm_channels_regular_file(output);
    }
    free(executable);
    return valid;
}

bool wm_channels_package_read(const char *folder, WmChannelPackage *package) {
    memset(package, 0, sizeof(*package));
    if (!wm_channels_directory(folder))
        return false;
    if (!realpath(folder, package->directory))
        return false;
    char path[4096];
    if (!wm_channels_join(path, package->directory, "channel.json") ||
        !wm_channels_regular_file(path)) {
        return false;
    }
    WmJson json;
    if (!wm_json_load(&json, path, 128 * 1024))
        return false;
    int version = 0;
    bool valid =
        wm_json_integer(&json, wm_json_member(&json, 0, "schemaVersion"), &version) &&
        version == 1 &&
        wm_json_copy(&json, wm_json_member(&json, 0, "id"), package->id,
                     sizeof(package->id)) &&
        wm_json_copy_text(&json, wm_json_member(&json, 0, "title"), package->title,
                          sizeof(package->title)) &&
        wm_local_channel_id_valid(package->id) && package->title[0];
    char icon[256], banner[256];
    valid = valid &&
            wm_json_copy(&json, wm_json_member(&json, 0, "iconLayout"), icon,
                         sizeof(icon)) &&
            wm_json_copy(&json, wm_json_member(&json, 0, "bannerLayout"), banner,
                         sizeof(banner)) &&
            strcmp(icon, "icon.json") == 0 && strcmp(banner, "banner.json") == 0;
    size_t audio = wm_json_member(&json, 0, "audio");
    if (valid && audio != WM_JSON_INVALID) {
        char source[64];
        valid = wm_json_copy(&json, wm_json_member(&json, audio, "src"), source,
                             sizeof(source)) &&
                strcmp(source, "sound.wav") == 0;
        size_t loop = wm_json_member(&json, audio, "loop");
        if (loop != WM_JSON_INVALID) {
            valid = valid && json.tokens[loop].type == WM_JSON_BOOLEAN &&
                    json.source[json.tokens[loop].start] == 'f';
        }
        package->has_audio = valid;
    }
    wm_json_free(&json);
    return valid;
}

bool wm_channels_texture_filename(const char *name) {
    size_t length = strlen(name);
    if (length < 5 || length > 120 || strcmp(name + length - 4, ".png") != 0)
        return false;
    for (size_t index = 0; index < length - 4; index++) {
        unsigned char character = (unsigned char)name[index];
        if (!(character >= 'A' && character <= 'Z') &&
            !(character >= 'a' && character <= 'z') &&
            !(character >= '0' && character <= '9') && character != '_' &&
            character != '-')
            return false;
    }
    return true;
}

static bool validate_layout_file(const char *folder, const char *name) {
    char path[4096];
    if (!wm_channels_join(path, folder, name) || !wm_channels_regular_file(path))
        return false;
    char error[256];
    WmLayout *layout = wm_layout_load_json(path, error, sizeof(error));
    if (!layout) {
        fprintf(stderr, "%s: %s\n", name, error);
        return false;
    }
    WmJson source_json;
    bool valid = wm_json_load(&source_json, path, 2 * 1024 * 1024);
    if (valid) {
        size_t textures = wm_json_member(&source_json, 0, "textures");
        size_t resources = wm_json_member(&source_json, 0, "resourceTextures");
        valid =
            textures < source_json.count &&
            source_json.tokens[textures].type == WM_JSON_ARRAY &&
            source_json.tokens[textures].children <= 16 &&
            source_json.tokens[textures].children == wm_layout_texture_count(layout) &&
            (resources == WM_JSON_INVALID ||
             (source_json.tokens[resources].type == WM_JSON_OBJECT &&
              source_json.tokens[resources].children == 0));
        wm_json_free(&source_json);
    }
    for (size_t index = 0; valid && index < wm_layout_texture_count(layout); index++) {
        const WmLayoutTexture *texture = wm_layout_texture_at(layout, index);
        char source[4096];
        valid = texture && wm_channels_texture_filename(texture->url) &&
                wm_channels_join(source, folder, texture->url) &&
                wm_channels_regular_file(source);
        if (!valid)
            break;
        char *bytes = NULL;
        size_t length = 0;
        valid = wm_regular_file_read(source, 8 * 1024 * 1024, &bytes, &length) ==
                WM_REGULAR_FILE_OK;
        WmImage image = {0};
        if (valid) {
            valid = wm_settings_png_decode((const uint8_t *)bytes, length, &image) &&
                    image.width == (uint32_t)texture->width &&
                    image.height == (uint32_t)texture->height;
        }
        wm_image_free(&image);
        free(bytes);
    }
    if (!valid)
        fprintf(stderr,
                "%s: use static bounded RGBA PNG textures with "
                "matching declared dimensions; resourceTextures are unsupported.\n",
                name);
    wm_layout_destroy(layout);
    return valid;
}

bool wm_channels_package_validate(const WmChannelPackage *package) {
    if (!validate_layout_file(package->directory, "icon.json") ||
        !validate_layout_file(package->directory, "banner.json"))
        return false;
    if (!package->has_audio)
        return true;
    char path[4096], error[128];
    WmAudioPcm pcm = {0};
    bool valid = wm_channels_join(path, package->directory, "sound.wav") &&
                 wm_channels_regular_file(path) &&
                 wm_audio_wav_read(path, &pcm, error, sizeof(error));
    wm_audio_pcm_free(&pcm);
    if (!valid)
        fputs("sound.wav: expected bounded mono/stereo PCM16 WAV.\n", stderr);
    return valid;
}

static bool copy_regular(const char *source, const char *destination) {
    if (!wm_channels_regular_file(source))
        return false;
    int input = open(source, O_RDONLY | O_NONBLOCK | O_NOFOLLOW);
    if (input < 0)
        return false;
    struct stat metadata;
    bool valid = fstat(input, &metadata) == 0 && S_ISREG(metadata.st_mode) &&
                 metadata.st_size > 0 && metadata.st_size <= 8 * 1024 * 1024;
    int output =
        valid ? open(destination, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600) : -1;
    valid = valid && output >= 0;
    char buffer[16384];
    while (valid) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0) {
            valid = false;
            break;
        }
        if (count == 0)
            break;
        size_t used = 0;
        while (used < (size_t)count) {
            ssize_t written = write(output, buffer + used, (size_t)count - used);
            if (written < 0 && errno == EINTR)
                continue;
            if (written <= 0) {
                valid = false;
                break;
            }
            used += (size_t)written;
        }
    }
    if (output >= 0) {
        if (fsync(output) != 0)
            valid = false;
        if (close(output) != 0)
            valid = false;
    }
    if (close(input) != 0)
        valid = false;
    if (!valid && output >= 0)
        unlink(destination);
    return valid;
}

static bool install_layout(const WmChannelPackage *package, const char *destination,
                           const char *name) {
    char source_path[4096], destination_path[4096];
    if (!wm_channels_join(source_path, package->directory, name) ||
        !wm_channels_join(destination_path, destination, name))
        return false;
    WmJson json;
    if (!wm_json_load(&json, source_path, 2 * 1024 * 1024))
        return false;
    size_t textures = wm_json_member(&json, 0, "textures");
    size_t resources = wm_json_member(&json, 0, "resourceTextures");
    bool valid = textures < json.count && json.tokens[textures].type == WM_JSON_ARRAY &&
                 json.tokens[textures].children <= 16 &&
                 (resources == WM_JSON_INVALID ||
                  (json.tokens[resources].type == WM_JSON_OBJECT &&
                   json.tokens[resources].children == 0));
    int descriptor =
        valid ? open(destination_path, O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW, 0600)
              : -1;
    FILE *output = descriptor >= 0 ? fdopen(descriptor, "wb") : NULL;
    if (!output && descriptor >= 0)
        close(descriptor);
    valid = valid && output != NULL;
    size_t cursor = 0;
    for (size_t index = 0; valid && index < json.tokens[textures].children; index++) {
        size_t entry = wm_json_index(&json, textures, index);
        size_t url = wm_json_member(&json, entry, "url");
        char filename[128], image_source[4096], image_target[4096];
        valid = wm_json_copy(&json, url, filename, sizeof(filename)) &&
                wm_channels_texture_filename(filename) &&
                wm_channels_join(image_source, package->directory, filename);
        if (!valid)
            break;
        char stem[128];
        strcpy(stem, filename);
        strcpy(stem + strlen(stem) - 4, ".wmra");
        valid = wm_channels_join(image_target, destination, stem);
        char *bytes = NULL;
        size_t length = 0;
        if (valid) {
            valid = wm_regular_file_read(image_source, 8 * 1024 * 1024, &bytes,
                                         &length) == WM_REGULAR_FILE_OK;
        }
        WmImage image = {0};
        if (valid)
            valid = wm_settings_png_decode((const uint8_t *)bytes, length, &image);
        int declared_width = 0;
        int declared_height = 0;
        if (valid) {
            valid = wm_json_integer(&json, wm_json_member(&json, entry, "width"),
                                    &declared_width) &&
                    wm_json_integer(&json, wm_json_member(&json, entry, "height"),
                                    &declared_height) &&
                    image.width == (uint32_t)declared_width &&
                    image.height == (uint32_t)declared_height;
        }
        if (valid)
            valid = wm_image_write(image_target, &image);
        wm_image_free(&image);
        free(bytes);
        if (!valid)
            break;
        valid = url < json.count && json.tokens[url].start >= cursor &&
                json.tokens[url].end <= json.length &&
                fwrite(json.source + cursor, 1, json.tokens[url].start - cursor,
                       output) == json.tokens[url].start - cursor &&
                fprintf(output, "custom-channels/%s/%s", package->id, filename) > 0;
        cursor = json.tokens[url].end;
    }
    if (valid) {
        valid = fwrite(json.source + cursor, 1, json.length - cursor, output) ==
                json.length - cursor;
    }
    if (output && fclose(output) != 0)
        valid = false;
    wm_json_free(&json);
    if (!valid)
        unlink(destination_path);
    return valid;
}

static bool clear_installed_layout(const WmChannelPackage *package,
                                   const char *destination, const char *name) {
    char source_path[4096], target[4096];
    if (!wm_channels_join(source_path, package->directory, name) ||
        !wm_channels_join(target, destination, name))
        return false;
    unlink(target);
    WmJson json;
    if (!wm_json_load(&json, source_path, 2 * 1024 * 1024))
        return false;
    size_t textures = wm_json_member(&json, 0, "textures");
    if (textures < json.count && json.tokens[textures].type == WM_JSON_ARRAY) {
        for (size_t index = 0; index < json.tokens[textures].children; index++) {
            size_t entry = wm_json_index(&json, textures, index);
            char filename[128];
            if (!wm_json_copy(&json, wm_json_member(&json, entry, "url"), filename,
                              sizeof(filename)) ||
                !wm_channels_texture_filename(filename))
                continue;
            strcpy(filename + strlen(filename) - 4, ".wmra");
            if (wm_channels_join(target, destination, filename))
                unlink(target);
        }
    }
    wm_json_free(&json);
    return true;
}

bool wm_channels_package_directory(char path[4096], const char *assets,
                                   const char *id) {
    char relative[128];
    if (!wm_local_channel_id_valid(id))
        return false;
    int length = snprintf(relative, sizeof(relative), "custom-channels/%s", id);
    return length > 0 && (size_t)length < sizeof(relative) &&
           wm_channels_join(path, assets, relative);
}

bool wm_channels_package_install(const char *assets, const WmChannelPackage *package) {
    char root[4096], destination[4096], source[4096], target[4096];
    if (!wm_channels_join(root, assets, "custom-channels") ||
        (mkdir(root, 0700) != 0 && errno != EEXIST) || !wm_channels_directory(root) ||
        !wm_channels_package_directory(destination, assets, package->id) ||
        mkdir(destination, 0700) != 0)
        return false;
    bool okay = wm_channels_join(source, package->directory, "channel.json") &&
                wm_channels_join(target, destination, "channel.json") &&
                copy_regular(source, target) &&
                install_layout(package, destination, "icon.json") &&
                install_layout(package, destination, "banner.json");
    bool audio_created = false;
    if (okay && package->has_audio) {
        char audio_directory[4096], audio_name[128];
        int length = snprintf(audio_name, sizeof(audio_name), "%s.wav", package->id);
        okay = length > 0 && (size_t)length < sizeof(audio_name) &&
               wm_channels_join(audio_directory, assets, "channel-audio") &&
               (mkdir(audio_directory, 0700) == 0 || errno == EEXIST) &&
               wm_channels_directory(audio_directory) &&
               wm_channels_join(source, package->directory, "sound.wav") &&
               wm_channels_join(target, audio_directory, audio_name) &&
               copy_regular(source, target);
        audio_created = okay;
    }
    if (!okay) {
        if (wm_channels_join(target, destination, "channel.json"))
            unlink(target);
        clear_installed_layout(package, destination, "icon.json");
        clear_installed_layout(package, destination, "banner.json");
        char audio_path[4096], relative[128];
        int length =
            snprintf(relative, sizeof(relative), "channel-audio/%s.wav", package->id);
        if (audio_created && length > 0 && (size_t)length < sizeof(relative) &&
            wm_channels_join(audio_path, assets, relative))
            unlink(audio_path);
        rmdir(destination);
    }
    return okay;
}

int wm_channels_open_slot_status(const char *assets) {
    WmMenu menu;
    wm_menu_init(&menu);
    if (!wm_catalog_load(&menu, assets))
        return -1;
    for (int slot = 1; slot < WM_SLOT_COUNT; slot++) {
        if (!menu.slots[slot].occupied)
            return 1;
    }
    return 0;
}

static bool installed_layout_ready(const char *assets, const char *id,
                                   const char *layout_path, const char *kind,
                                   bool imported) {
    char error[256];
    WmLayout *layout = wm_layout_load_json(layout_path, error, sizeof(error));
    if (!layout)
        return false;

    char prefix[128];
    int length = imported ? snprintf(prefix, sizeof(prefix),
                                     "channel-layouts/%s/%s/textures/", id, kind)
                          : snprintf(prefix, sizeof(prefix), "custom-channels/%s/", id);
    bool ready = length > 0 && (size_t)length < sizeof(prefix);
    for (size_t index = 0; ready && index < wm_layout_texture_count(layout); index++) {
        const WmLayoutTexture *texture = wm_layout_texture_at(layout, index);
        const char *url = texture ? texture->url : "";
        size_t prefix_length = strlen(prefix);
        size_t url_length = strlen(url);
        if (url_length <= prefix_length + 4 ||
            strncmp(url, prefix, prefix_length) != 0 ||
            strcmp(url + url_length - 4, ".png") != 0 || texture->missing) {
            ready = false;
            break;
        }
        const char *filename = url + prefix_length;
        if (filename[0] == '.' || strchr(filename, '/') || strchr(filename, '\\')) {
            ready = false;
            break;
        }
        char relative[512], image[4096];
        memcpy(relative, url, url_length - 4);
        memcpy(relative + url_length - 4, ".wmra", sizeof(".wmra"));
        ready = wm_channels_join(image, assets, relative) &&
                wm_channels_regular_file(image);
    }
    wm_layout_destroy(layout);
    return ready;
}

bool wm_channels_installed_ready(const char *assets, const char *id, bool imported) {
    char root[4096], icon[4096], banner[4096];
    if (imported) {
        char relative[128];
        int length = snprintf(relative, sizeof(relative), "channel-layouts/%s", id);
        if (length <= 0 || (size_t)length >= sizeof(relative) ||
            !wm_channels_join(root, assets, relative) ||
            !wm_channels_join(icon, root, "icon/icon.json") ||
            !wm_channels_join(banner, root, "banner/banner.json"))
            return false;
    } else {
        WmChannelPackage package;
        if (!wm_channels_package_directory(root, assets, id) ||
            !wm_channels_package_read(root, &package) || strcmp(package.id, id) != 0 ||
            !wm_channels_join(icon, root, "icon.json") ||
            !wm_channels_join(banner, root, "banner.json"))
            return false;
        if (package.has_audio) {
            char relative[128], audio[4096];
            int length =
                snprintf(relative, sizeof(relative), "channel-audio/%s.wav", id);
            if (length <= 0 || (size_t)length >= sizeof(relative) ||
                !wm_channels_join(audio, assets, relative) ||
                !wm_channels_regular_file(audio))
                return false;
        }
    }
    return wm_channels_regular_file(icon) && wm_channels_regular_file(banner) &&
           installed_layout_ready(assets, id, icon, "icon", imported) &&
           installed_layout_ready(assets, id, banner, "banner", imported);
}
