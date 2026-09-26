#ifndef WII_MENU_RESOURCE_BMG_H
#define WII_MENU_RESOURCE_BMG_H

#include <stddef.h>
#include <stdint.h>

typedef struct WmBmg WmBmg;

/* Parse a big-endian UTF-16 BMG into owned UTF-8 message strings. Control
 * packets remain in the owned source bytes and are omitted from plain text,
 * matching the HTML reference's message lookup. Input is borrowed. */
WmBmg *wm_bmg_parse(const uint8_t *data, size_t size,
                    char *error, size_t error_capacity);

/* Read a locally prepared BMG, or the locale's BMG inside an ignored asset
 * directory. The locale is exactly three ASCII letters, such as "eng". */
WmBmg *wm_bmg_load_file(const char *path,
                        char *error, size_t error_capacity);
WmBmg *wm_bmg_load_assets(const char *assets_directory, const char *locale,
                          char *error, size_t error_capacity);
void wm_bmg_destroy(WmBmg *bmg);

size_t wm_bmg_count(const WmBmg *bmg);
const char *wm_bmg_text(const WmBmg *bmg, unsigned message_id);
/* Direct adapter for WmSdMessageProvider and other scene callbacks. */
const char *wm_bmg_message(void *context, unsigned message_id);

/* Source INF1 metadata is retained for future controllers that use it.
 * Returned bytes remain valid until wm_bmg_destroy. */
const uint8_t *wm_bmg_attributes(const WmBmg *bmg, unsigned message_id,
                                  size_t *size);

#endif
