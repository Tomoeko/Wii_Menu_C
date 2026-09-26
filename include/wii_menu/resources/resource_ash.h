#ifndef WII_MENU_RESOURCE_ASH_H
#define WII_MENU_RESOURCE_ASH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Always returns a newly allocated buffer, including for uncompressed input.
 * The caller frees it with free(). ASH0 output is limited by its 24-bit size. */
bool wm_ash_decode(const uint8_t *data, size_t size,
                   uint8_t **output, size_t *output_size,
                   char *error, size_t error_size);

#endif
