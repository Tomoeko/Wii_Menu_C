#ifndef WII_MENU_SHARED_FONT_EXPORT_H
#define WII_MENU_SHARED_FONT_EXPORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Export the two optional shared Wii RFNA fonts from a decrypted U8 archive.
 * The native assets directory receives wbf1/wbf2 and their layout aliases
 * under fonts/. The source bytes remain caller-owned and are never modified. */
bool wm_shared_font_export(const uint8_t *archive_bytes, size_t archive_size,
                           const char *assets_directory,
                           char *error, size_t error_capacity);

#endif
