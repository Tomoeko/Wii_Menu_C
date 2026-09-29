#ifndef WM_SUPPORT_ERROR_H
#define WM_SUPPORT_ERROR_H

#include <stddef.h>

/* An optional error buffer receives a truncated, NUL-terminated message. */
void wm_error_set(char *error, size_t capacity, const char *message);

#endif
