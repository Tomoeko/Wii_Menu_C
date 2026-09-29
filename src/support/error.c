#include "wii_menu/support/error.h"

#include <stdio.h>

void wm_error_set(char *error, size_t capacity, const char *message) {
    if (error && capacity)
        snprintf(error, capacity, "%s", message);
}
