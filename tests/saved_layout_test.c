#include "wii_menu/saved_layout.h"

#include <assert.h>
#include <string.h>

int main(void) {
    uint8_t save[WM_SAVED_LAYOUT_BYTES] = {0};
    memcpy(save, "RIPL", 4);
    save[6] = 0x04;
    save[7] = 0xc0;
    save[11] = 3;
    save[15] = 2;
    save[0x10] = 1;
    save[0x20] = 3;
    const uint8_t channel_id[8] = {0, 1, 0, 1, 0x48, 0x41, 0x42, 0x41};
    memcpy(save + 0x28, channel_id, sizeof(channel_id));
    /* Independent hashlib MD5 of the first 0x4b0 fixture bytes. */
    const uint8_t checksum[16] = {
        0x38, 0x3a, 0x20, 0x9c, 0xe0, 0x73, 0xd3, 0x3b,
        0x07, 0x00, 0xd9, 0xbe, 0x1c, 0xa9, 0xc3, 0xe4
    };
    memcpy(save + sizeof(save) - sizeof(checksum), checksum,
           sizeof(checksum));
    WmSavedLayout layout;
    char error[100];
    assert(wm_saved_layout_parse(save, sizeof(save), &layout,
                                  error, sizeof(error)));
    assert(layout.previous_page == 2);
    assert(strcmp(layout.slots[0].id, "disc") == 0);
    assert(strcmp(layout.slots[1].id, "0001000148414241") == 0);
    assert(layout.slots[2].id[0] == '\0');

    save[0x20] ^= 1;
    assert(!wm_saved_layout_parse(save, sizeof(save), &layout,
                                   error, sizeof(error)));
    assert(strcmp(error, "RIPL checksum mismatch") == 0);
    save[0x20] ^= 1;
    save[11] = 4;
    assert(!wm_saved_layout_parse(save, sizeof(save), &layout,
                                   error, sizeof(error)));
    save[11] = 3;
    assert(!wm_saved_layout_parse(save, sizeof(save) - 1, &layout,
                                   error, sizeof(error)));
    return 0;
}
