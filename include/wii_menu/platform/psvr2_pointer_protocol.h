#ifndef WII_MENU_PSVR2_POINTER_PROTOCOL_H
#define WII_MENU_PSVR2_POINTER_PROTOCOL_H

#include "wii_menu/platform/platform.h"

#include <stddef.h>

/* CT preserves the framing expected by the existing Stage3 ttyGS1 bridge.
 * All multibyte fields are little endian. A packet is a complete snapshot,
 * not a relative delta; separate button counters detect lost click edges. */
enum {
    WM_PSVR2_POINTER_PACKET_SIZE = 16,
    WM_PSVR2_POINTER_ACTIVE = 1,
    WM_PSVR2_POINTER_RESET = 2,
    WM_PSVR2_POINTER_LEFT = 1,
    WM_PSVR2_POINTER_RIGHT = 2,
    WM_PSVR2_POINTER_WATCHDOG_MS = 250,
    WM_PSVR2_POINTER_EVENTS = 16
};

typedef struct WmPsvr2PointerState {
    uint16_t session;
    uint16_t sequence;
    uint16_t x;
    uint16_t y;
    uint8_t flags;
    uint8_t buttons;
    uint8_t left_edges;
    uint8_t right_edges;
} WmPsvr2PointerState;

typedef struct WmPsvr2PointerDecoder {
    uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE];
    size_t byte_count;
    WmPsvr2PointerState previous;
    bool have_session;
    bool visible;
    bool suppress_buttons;
    uint8_t held_buttons;
    int last_x;
    int last_y;
    uint64_t last_packet_ms;
    WmEvent events[WM_PSVR2_POINTER_EVENTS];
    unsigned event_head;
    unsigned event_count;
    uint64_t rejected_packets;
    uint64_t lost_button_edges;
} WmPsvr2PointerDecoder;

void wm_psvr2_pointer_encode(uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE],
                             const WmPsvr2PointerState *state);
bool wm_psvr2_pointer_decode(const uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE],
                              WmPsvr2PointerState *state);
/* Map the entire resizable host content area to the menu's anamorphic raster.
 * The host window's initial aspect is 16:9; dimensions do not alter logical
 * 640 x 456 coordinates. Outside/invalid positions return false. */
bool wm_psvr2_pointer_normalize(double x, double y, double width, double height,
                                 uint16_t *normalized_x,
                                 uint16_t *normalized_y);
void wm_psvr2_pointer_decoder_init(WmPsvr2PointerDecoder *decoder);
/* Returns bytes consumed. Drain events and feed the residue if it stops early;
 * this bounds storage without discarding queued button transitions. */
size_t wm_psvr2_pointer_decoder_feed(WmPsvr2PointerDecoder *decoder,
                                      const uint8_t *bytes, size_t size,
                                      uint64_t now_ms);
bool wm_psvr2_pointer_decoder_poll(WmPsvr2PointerDecoder *decoder,
                                    WmEvent *event);
void wm_psvr2_pointer_decoder_tick(WmPsvr2PointerDecoder *decoder,
                                   uint64_t now_ms);
void wm_psvr2_pointer_decoder_disconnect(WmPsvr2PointerDecoder *decoder);

#endif
