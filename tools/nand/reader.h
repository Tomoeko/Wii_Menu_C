#ifndef WII_MENU_NAND_READER_H
#define WII_MENU_NAND_READER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct WmNandSummary {
    uint32_t generation;
    size_t discovered_titles;
    size_t shared_font_archives;
    size_t extracted_files;
    size_t extracted_bytes;
} WmNandSummary;

/* Read a BootMii-style Wii NAND dump and publish authenticated installed-title
 * content under a new output directory. Channel titles are discovered by an
 * IMET header in an .app file. For each discovered title, every local .app and
 * title.tmd is extracted in title/<8hex>/<8hex>/content/. A shared1 archive
 * containing both authenticated RFNA Wii bitmap fonts is also included when
 * present, along with the System Menu's iplsave.bin. Source, key file, and
 * output paths are supplied by the caller; none are written to a manifest.
 *
 * keys_path may be NULL. A dump key footer is preferred; otherwise a sibling
 * keys.bin is used. No output is published unless every selected file passes
 * HMAC-SHA1 authentication. The caller owns error and summary. */
bool wm_nand_extract_channels(const char *source_path,
                               const char *keys_path,
                               const char *output_directory,
                               WmNandSummary *summary,
                               char *error, size_t error_capacity);

#endif
