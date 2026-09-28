#ifndef WM_PREPARATION_PREPARE_FS_H
#define WM_PREPARATION_PREPARE_FS_H

#include <stdbool.h>
#include <stddef.h>

/* These paths are private staging paths, never strings from resource files. */
enum { PREPARE_PATH_CAPACITY = 4096 };

bool path_join(char *result, size_t capacity, const char *directory, const char *name);
bool publish_directory_no_replace(const char *source, const char *destination);
bool validate_parents(const char *path, bool create_missing);
bool make_file_parent(const char *path);
bool remove_tree(const char *path);
bool copy_file(const char *source, const char *destination);
bool copy_tree(const char *source, const char *destination);
bool regular_tree(const char *path);

#endif
