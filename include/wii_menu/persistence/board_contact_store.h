#ifndef WII_MENU_BOARD_CONTACT_STORE_H
#define WII_MENU_BOARD_CONTACT_STORE_H

#include <stdbool.h>
#include <stddef.h>

enum { WM_BOARD_CONTACT_CAPACITY = 100 };

typedef struct WmBoardContactStore WmBoardContactStore;

typedef struct WmBoardContact {
    bool wii;
    bool confirmed;
    const char *address;
    const char *nickname;
} WmBoardContact;

typedef enum WmBoardContactStoreStatus {
    WM_BOARD_CONTACT_STORE_OK,
    WM_BOARD_CONTACT_STORE_MISSING,
    WM_BOARD_CONTACT_STORE_ERROR
} WmBoardContactStoreStatus;

/* Read the maintained HTML Address Book's JSON slot array from a local file.
 * An absent path starts an empty book. Malformed files are never overwritten.
 * The caller owns the returned store; no source WAD or console identity is
 * stored here. */
WmBoardContactStore *wm_board_contact_store_open(
    const char *path, WmBoardContactStoreStatus *status,
    char *error, size_t error_capacity);
void wm_board_contact_store_destroy(WmBoardContactStore *store);
size_t wm_board_contact_store_length(const WmBoardContactStore *store);
size_t wm_board_contact_store_occupied(const WmBoardContactStore *store);
bool wm_board_contact_store_get(const WmBoardContactStore *store,
                                size_t slot, WmBoardContact *contact);

/* Save into the first empty slot or append. Existing slot positions and
 * unknown JSON metadata are retained. The loaded file must still match the
 * read baseline; on any failure the in-memory book stays unchanged. */
bool wm_board_contact_store_register(WmBoardContactStore *store,
                                     WmBoardContact contact, size_t *slot,
                                     char *error, size_t error_capacity);

/* Change only the nickname field, retaining the contact's other JSON fields
 * and its slot. Erase writes null at the same slot without moving later
 * contacts. Both operations reject a changed file and leave memory intact on
 * failure. */
bool wm_board_contact_store_rename(WmBoardContactStore *store, size_t slot,
                                   const char *nickname, char *error,
                                   size_t error_capacity);
bool wm_board_contact_store_erase(WmBoardContactStore *store, size_t slot,
                                  char *error, size_t error_capacity);

#endif
