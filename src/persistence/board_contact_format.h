#ifndef WII_MENU_BOARD_CONTACT_FORMAT_H
#define WII_MENU_BOARD_CONTACT_FORMAT_H

#include "wii_menu/persistence/board_contact_store.h"
#include "wii_menu/support/json.h"

#include <stdbool.h>
#include <stddef.h>

enum {
    CONTACT_MAX_JSON_BYTES = 512 * 1024,
    CONTACT_ADDRESS_BYTES = 400,
    CONTACT_NICKNAME_BYTES = 44
};

/* source_json is an owned copy of the original object, including unknown
 * fields. The store frees it when a slot changes or the book is destroyed. */
typedef struct StoredContact {
    bool occupied;
    bool wii;
    bool confirmed;
    char address[CONTACT_ADDRESS_BYTES];
    char nickname[CONTACT_NICKNAME_BYTES];
    char *source_json;
} StoredContact;

typedef enum ContactRewriteStatus {
    CONTACT_REWRITE_OK,
    CONTACT_REWRITE_INVALID_SOURCE,
    CONTACT_REWRITE_ERROR
} ContactRewriteStatus;

bool contact_json_utf8_valid(const char *text, size_t length);
bool contact_nickname_valid(const char *text);
bool contact_stored_address_valid(bool wii, const char *text);
bool contact_parse(const WmJson *json, size_t token, StoredContact *contact);

/* On success, the caller owns output. On failure output remains NULL. */
bool contact_serialize(WmBoardContact contact, char **output);
ContactRewriteStatus contact_rewrite_nickname(const char *original,
                                              const char *nickname,
                                              char **output);
bool contact_build_array(const StoredContact slots[WM_BOARD_CONTACT_CAPACITY],
                         size_t current_length, size_t slot,
                         const char *replacement, char **output,
                         size_t *output_length);

#endif
