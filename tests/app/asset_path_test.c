#define _XOPEN_SOURCE 700
#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "asset_path.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void join_path(char *result, const char *root, const char *suffix)
{
    int length = snprintf(result, WM_APP_ASSET_PATH_CAPACITY, "%s/%s", root, suffix);
    assert(length > 0 && length < WM_APP_ASSET_PATH_CAPACITY);
}

static void make_directory(const char *root, const char *suffix)
{
    char path[WM_APP_ASSET_PATH_CAPACITY];
    join_path(path, root, suffix);
    assert(mkdir(path, 0700) == 0);
}

static void remove_directory(const char *root, const char *suffix)
{
    char path[WM_APP_ASSET_PATH_CAPACITY];
    join_path(path, root, suffix);
    assert(rmdir(path) == 0);
}

static void make_executable(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    assert(fclose(file) == 0);
    assert(chmod(path, 0700) == 0);
}

int main(void)
{
    char original_directory[WM_APP_ASSET_PATH_CAPACITY];
    assert(getcwd(original_directory, sizeof(original_directory)));
    const char *path_environment = getenv("PATH");
    char *original_path = path_environment ? strdup(path_environment) : NULL;
    assert(!path_environment || original_path);

    char temporary[] = "/tmp/wii-menu-assets-XXXXXX";
    assert(mkdtemp(temporary));
    /* Canonicalize /tmp for platforms where it is a symlink. */
    char *root = realpath(temporary, NULL);
    assert(root);
    make_directory(root, "launch");
    make_directory(root, "project");
    make_directory(root, "project/.local");
    make_directory(root, "project/.local/native-assets");
    make_directory(root, "project/build");

    char launch[WM_APP_ASSET_PATH_CAPACITY];
    char executable[WM_APP_ASSET_PATH_CAPACITY];
    char expected[WM_APP_ASSET_PATH_CAPACITY];
    char found[WM_APP_ASSET_PATH_CAPACITY];
    join_path(launch, root, "launch");
    join_path(executable, root, "project/build/wii-menu");
    join_path(expected, root, "project/.local/native-assets");
    make_executable(executable);

    /* An unrelated launch directory still finds assets near the executable. */
    assert(chdir(launch) == 0);
    assert(wm_app_find_default_assets(executable, found, sizeof(found)));
    assert(strcmp(found, expected) == 0);
    char too_small[2] = "x";
    assert(!wm_app_find_default_assets(executable, too_small, sizeof(too_small)));
    assert(too_small[0] == '\0');

    char build[WM_APP_ASSET_PATH_CAPACITY];
    join_path(build, root, "project/build");
    assert(setenv("PATH", build, 1) == 0);
    assert(wm_app_find_default_assets("wii-menu", found, sizeof(found)));
    assert(strcmp(found, expected) == 0);
    /* A searchable directory named like the command is not an executable. */
    make_directory(root, "launch/wii-menu");
    char search_path[WM_APP_ASSET_PATH_CAPACITY * 2];
    int path_length = snprintf(search_path, sizeof(search_path), "%s:%s", launch, build);
    assert(path_length > 0 && (size_t)path_length < sizeof(search_path));
    assert(setenv("PATH", search_path, 1) == 0);
    assert(wm_app_find_default_assets("wii-menu", found, sizeof(found)));
    assert(strcmp(found, expected) == 0);
    assert(chdir(root) == 0);
    assert(wm_app_find_default_assets("project/build/wii-menu", found, sizeof(found)));
    assert(strcmp(found, expected) == 0);

    /* Finder launches the binary inside Contents/MacOS; walk out of the bundle. */
    make_directory(root, "project/build/wii-menu.app");
    make_directory(root, "project/build/wii-menu.app/Contents");
    make_directory(root, "project/build/wii-menu.app/Contents/MacOS");
    char bundle_executable[WM_APP_ASSET_PATH_CAPACITY];
    join_path(bundle_executable, root,
              "project/build/wii-menu.app/Contents/MacOS/wii-menu");
    make_executable(bundle_executable);
    assert(chdir(launch) == 0);
    assert(wm_app_find_default_assets(bundle_executable, found, sizeof(found)));
    assert(strcmp(found, expected) == 0);

    /* Current-directory assets win over the executable's tree. */
    make_directory(root, "launch/.local");
    make_directory(root, "launch/.local/native-assets");
    make_directory(root, "launch/child");
    join_path(expected, root, "launch/.local/native-assets");
    assert(wm_app_find_default_assets(executable, found, sizeof(found)));
    assert(strcmp(found, expected) == 0);
    char child[WM_APP_ASSET_PATH_CAPACITY];
    join_path(child, root, "launch/child");
    assert(chdir(child) == 0);
    assert(wm_app_find_default_assets(NULL, found, sizeof(found)));
    assert(strcmp(found, expected) == 0);

    assert(chdir(root) == 0);
    remove_directory(root, "launch/child");
    remove_directory(root, "launch/.local/native-assets");
    remove_directory(root, "launch/.local");
    remove_directory(root, "project/.local/native-assets");
    join_path(expected, root, "project/.local/native-assets");
    make_executable(expected);
    assert(chdir(launch) == 0);
    assert(!wm_app_find_default_assets(executable, found, sizeof(found)));
    assert(found[0] == '\0');
    assert(!wm_app_find_default_assets("missing-command", found, sizeof(found)));
    assert(!wm_app_find_default_assets(executable, NULL, sizeof(found)));
    assert(!wm_app_find_default_assets(executable, found, 0));

    assert(chdir(original_directory) == 0);
    if (original_path) assert(setenv("PATH", original_path, 1) == 0);
    else assert(unsetenv("PATH") == 0);
    free(original_path);
    assert(unlink(expected) == 0);
    assert(unlink(executable) == 0);
    assert(unlink(bundle_executable) == 0);
    remove_directory(root, "project/build/wii-menu.app/Contents/MacOS");
    remove_directory(root, "project/build/wii-menu.app/Contents");
    remove_directory(root, "project/build/wii-menu.app");
    remove_directory(root, "project/build");
    remove_directory(root, "project/.local");
    remove_directory(root, "project");
    remove_directory(root, "launch/wii-menu");
    remove_directory(root, "launch");
    assert(rmdir(root) == 0);
    free(root);
    puts("Default asset discovery tests passed.");
    return 0;
}
