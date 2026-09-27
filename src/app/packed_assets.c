#define _POSIX_C_SOURCE 200809L
#define _XOPEN_SOURCE 700
#define _DARWIN_C_SOURCE 1

#include "packed_assets.h"
#include "wii_menu/resources/wm_pack.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool report_error(char *error, size_t capacity, const char *message) {
    if (error && capacity) snprintf(error, capacity, "%s", message);
    return false;
}

/* Only called on the private mkdtemp tree owned by this invocation. Links
 * are unlinked without following them, including after malformed extraction. */
static void remove_owned_tree(const char *path) {
    struct stat status;
    if (lstat(path, &status) != 0) return;
    if (!S_ISDIR(status.st_mode)) {
        unlink(path);
        return;
    }
    DIR *directory = opendir(path);
    if (!directory) return;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;
        char child[4096];
        int length = snprintf(child, sizeof(child), "%s/%s", path, entry->d_name);
        if (length > 0 && (size_t)length < sizeof(child)) remove_owned_tree(child);
    }
    closedir(directory);
    rmdir(path);
}

void wm_packed_assets_close(WmPackedAssets *assets) {
    if (!assets) return;
    if (assets->temporary_root[0]) remove_owned_tree(assets->temporary_root);
    free(assets->argv);
    memset(assets, 0, sizeof(*assets));
}

bool wm_packed_assets_open(WmPackedAssets *assets, int argc, char **argv,
                           char *error, size_t error_capacity) {
    if (!assets || argc < 1 || !argv)
        return report_error(error, error_capacity, "Invalid application arguments.");
    memset(assets, 0, sizeof(*assets));
    assets->argc = argc;
    assets->argv = calloc((size_t)argc + 3, sizeof(*assets->argv));
    if (!assets->argv)
        return report_error(error, error_capacity, "Could not allocate application arguments.");
    memcpy(assets->argv, argv, (size_t)argc * sizeof(*argv));
    const char *package = NULL;
    int argument = -1;
    bool layout = false;
    for (int index = 1; index < argc; index++) {
        if (!strcmp(argv[index], "--help") || !strcmp(argv[index], "-h")) return true;
        if (!strcmp(argv[index], "--layout")) layout = true;
        if (!strcmp(argv[index], "--assets")) {
            if (index + 1 == argc)
                return report_error(error, error_capacity, "--assets requires a directory or .wm file.");
            argument = ++index;
        }
    }
    if (argument >= 0) {
        size_t length = strlen(argv[argument]);
        if (length >= 3 && !strcmp(argv[argument] + length - 3, ".wm"))
            package = argv[argument];
    }
#ifdef WM_PLATFORM_PSVR2
    if (argument < 0 && !layout) {
        char executable[4096];
        ssize_t length = readlink("/proc/self/exe", executable, sizeof(executable) - 1);
        if (length < 1 || (size_t)length >= sizeof(executable) - 1)
            return report_error(error, error_capacity, "Could not locate the executable's adjacent .wm package.");
        executable[length] = '\0';
        if (!wm_pack_adjacent(executable, assets->package_path, sizeof(assets->package_path)))
            return report_error(error, error_capacity, "The adjacent .wm path is too long.");
        package = assets->package_path;
        assets->argv[assets->argc++] = "--assets";
        argument = assets->argc++;
    }
#else
    (void)layout;
#endif
    if (!package) return true;
    snprintf(assets->temporary_root, sizeof(assets->temporary_root),
             "/tmp/wii-menu-assets-XXXXXX");
    if (!mkdtemp(assets->temporary_root)) {
        assets->temporary_root[0] = '\0';
        return report_error(error, error_capacity, "Could not create the RAM asset directory.");
    }
    /* macOS exposes /tmp through a symlink. The package parser uses physical
     * directory parents, so retain the canonical name of our private tree. */
    char *canonical = realpath(assets->temporary_root, NULL);
    if (!canonical || strlen(canonical) >= sizeof(assets->temporary_root)) {
        free(canonical);
        return report_error(error, error_capacity, "Could not resolve the private asset directory.");
    }
    memcpy(assets->temporary_root, canonical, strlen(canonical) + 1);
    free(canonical);
    snprintf(assets->assets_path, sizeof(assets->assets_path), "%s/assets",
             assets->temporary_root);
    if (!wm_pack_extract(package, assets->assets_path, error, error_capacity)) return false;
    assets->argv[argument] = assets->assets_path;
#ifdef WM_PLATFORM_PSVR2
    if (!getenv("WM_PSVR2_CONFIG")) {
        char config[4096];
        int length = snprintf(config, sizeof(config), "%s/psvr2/vr.conf", assets->assets_path);
        if (length > 0 && (size_t)length < sizeof(config) && access(config, R_OK) == 0) {
            if (setenv("WM_PSVR2_CONFIG", config, 1) != 0)
                return report_error(error, error_capacity, "Could not select the packed VR configuration.");
        }
    }
#endif
    return true;
}
