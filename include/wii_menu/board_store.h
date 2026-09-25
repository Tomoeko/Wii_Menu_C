#ifndef WII_MENU_BOARD_STORE_H
#define WII_MENU_BOARD_STORE_H

#include "wii_menu/board_scene.h"

#include <stddef.h>

typedef enum WmBoardStoreStatus {
    WM_BOARD_STORE_OK,
    WM_BOARD_STORE_MISSING,
    WM_BOARD_STORE_ERROR
} WmBoardStoreStatus;

/* The caller supplies a local state path in an existing writable directory.
 * A missing file leaves the Board untouched; malformed input also leaves the
 * Board untouched and returns ERROR. Version 1 files may omit createdAtMs;
 * date-only records then sort after timestamped records, retaining file order
 * for ties. The file contains only local memo data, never the source WAD or
 * NAND. */
WmBoardStoreStatus wm_board_store_load(const char *path, WmBoardScene *board,
                                       char *error, size_t error_capacity);

/* Write through a private same-directory temporary file and atomically
 * replace path after the data reaches disk. Saves the complete Board state. */
bool wm_board_store_save(const char *path, const WmBoardScene *board,
                         char *error, size_t error_capacity);

#endif
