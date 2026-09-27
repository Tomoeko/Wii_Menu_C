#ifndef WII_MENU_RESOURCES_WM_PACK_H
#define WII_MENU_RESOURCES_WM_PACK_H

#include <stdbool.h>
#include <stddef.h>

/* Version 1 is a little-endian, uncompressed sequence of regular files.
 * Limits bound RAM/disk use before loading private converted assets. */
#define WM_PACK_MAX_FILES 65536u
#define WM_PACK_MAX_DIRECTORIES 65536u
#define WM_PACK_MAX_PATH 1023u
#define WM_PACK_MAX_FILE_BYTES (128u * 1024u * 1024u)
#define WM_PACK_MAX_BYTES (256u * 1024u * 1024u)

/* Creates a complete package beside its destination, then atomically replaces
 * a regular destination file. Neither links nor special source files are packed. */
bool wm_pack_create(const char *source_directory, const char *package_path,
                    char *error, size_t error_capacity);

/* Validates checksums, names and limits while extracting into private staging.
 * Publishes only a complete tree. directory must not already exist. */
bool wm_pack_extract(const char *package_path, const char *directory,
                     char *error, size_t error_capacity);

/* foo, foo.elf and foo.bin all discover foo.wm in the same directory. */
bool wm_pack_adjacent(const char *executable_path, char *package_path,
                      size_t capacity);

#endif
