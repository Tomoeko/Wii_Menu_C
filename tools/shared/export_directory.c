#define _POSIX_C_SOURCE 200809L
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE 1
#endif

#include "export_directory.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int open_directory(int parent, const char *component, mode_t mode,
                          bool create)
{
    int directory = openat(parent, component,
                           O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (directory < 0 && errno == ENOENT && create) {
        if (mkdirat(parent, component, mode) == 0 || errno == EEXIST) {
            directory = openat(parent, component,
                               O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        }
    }
    return directory;
}

/* O_NOFOLLOW only protects the final component. A path ending in "/." or
 * "/.." would make a preceding symlink an unprotected parent. */
static bool valid_root_leaf(const char *path)
{
    const char *leaf = strrchr(path, '/');
    leaf = leaf ? leaf + 1 : path;
    return strcmp(leaf, "..") != 0 &&
           (strcmp(leaf, ".") != 0 || strcmp(path, ".") == 0);
}

bool wm_export_directory_root(const char *path, mode_t mode)
{
    if (!path || !path[0]) return false;

    size_t path_length = strlen(path);
    if (path_length == SIZE_MAX) return false;
    char *components = malloc(path_length + 1);
    if (!components) return false;
    memcpy(components, path, path_length + 1);

    while (path_length > 1 && components[path_length - 1] == '/') {
        components[--path_length] = '\0';
    }
    if (strcmp(components, "/") == 0) {
        int directory = open("/", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        free(components);
        return directory >= 0 && close(directory) == 0;
    }
    if (!valid_root_leaf(components)) {
        free(components);
        return false;
    }

    char *name = strrchr(components, '/');
    const char *parent_path = ".";
    if (name) {
        *name++ = '\0';
        parent_path = components[0] ? components : "/";
    } else {
        name = components;
    }
    int parent = open(parent_path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int directory = parent >= 0
                        ? open_directory(parent, name, mode, true) : -1;
    bool valid = directory >= 0;
    if (directory >= 0 && close(directory) != 0) valid = false;
    if (parent >= 0 && close(parent) != 0) valid = false;
    free(components);
    return valid;
}

bool wm_export_directory_child(const char *root, const char *relative,
                               mode_t mode)
{
    if (!root || !root[0] || !relative || !relative[0] ||
        relative[0] == '/') return false;

    size_t root_length = strlen(root);
    size_t path_length = strlen(relative);
    if (root_length == SIZE_MAX || path_length == SIZE_MAX) return false;
    while (root_length > 1 && root[root_length - 1] == '/') root_length--;
    char *root_path = malloc(root_length + 1);
    char *components = malloc(path_length + 1);
    if (!root_path || !components) {
        free(root_path);
        free(components);
        return false;
    }
    memcpy(root_path, root, root_length);
    root_path[root_length] = '\0';
    memcpy(components, relative, path_length + 1);

    if (!valid_root_leaf(root_path)) {
        free(root_path);
        free(components);
        return false;
    }

    int directory = open(root_path,
                         O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    free(root_path);
    if (directory < 0) {
        free(components);
        return false;
    }

    char *cursor = components;
    bool valid = true;
    while (*cursor && valid) {
        char *component = cursor;
        while (*cursor && *cursor != '/') cursor++;
        if (*cursor) {
            *cursor++ = '\0';
            while (*cursor == '/') cursor++;
        }
        bool final_component = *cursor == '\0';
        if (strcmp(component, ".") == 0 ||
            strcmp(component, "..") == 0) {
            valid = false;
            break;
        }
        int next = open_directory(directory, component, mode,
                                  final_component);
        if (next < 0) {
            valid = false;
        } else {
            close(directory);
            directory = next;
        }
    }
    if (close(directory) != 0) valid = false;
    free(components);
    return valid;
}
