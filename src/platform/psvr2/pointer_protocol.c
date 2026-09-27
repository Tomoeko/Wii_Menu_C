#include "wii_menu/platform/psvr2_pointer_protocol.h"

#include <math.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *bytes) {
    return (uint16_t)(bytes[0] | ((uint16_t)bytes[1] << 8));
}

static void write_u16(uint8_t *bytes, uint16_t value) {
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static uint16_t packet_crc(const uint8_t *bytes) {
    uint16_t crc = 0xffff;
    for (unsigned i = 0; i < 14; i++) {
        crc ^= (uint16_t)bytes[i] << 8;
        for (unsigned bit = 0; bit < 8; bit++) {
            crc = (uint16_t)((crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1);
        }
    }
    return crc;
}

void wm_psvr2_pointer_encode(uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE],
                             const WmPsvr2PointerState *state) {
    bytes[0] = 'C';
    bytes[1] = 'T';
    bytes[2] = (uint8_t)(0x10 | state->flags);
    bytes[3] = state->buttons;
    write_u16(bytes + 4, state->session);
    write_u16(bytes + 6, state->sequence);
    write_u16(bytes + 8, state->x);
    write_u16(bytes + 10, state->y);
    bytes[12] = state->left_edges;
    bytes[13] = state->right_edges;
    write_u16(bytes + 14, packet_crc(bytes));
}

bool wm_psvr2_pointer_decode(const uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE],
                              WmPsvr2PointerState *state) {
    if (!bytes || !state || bytes[0] != 'C' || bytes[1] != 'T' ||
        (bytes[2] & 0xfc) != 0x10 || (bytes[3] & ~3) != 0 ||
        (!(bytes[2] & WM_PSVR2_POINTER_ACTIVE) && bytes[3] != 0) ||
        ((bytes[2] & WM_PSVR2_POINTER_RESET) &&
         (bytes[2] & WM_PSVR2_POINTER_ACTIVE)) ||
        read_u16(bytes + 14) != packet_crc(bytes)) return false;
    *state = (WmPsvr2PointerState){
        .flags = bytes[2] & 3,
        .buttons = bytes[3],
        .session = read_u16(bytes + 4),
        .sequence = read_u16(bytes + 6),
        .x = read_u16(bytes + 8),
        .y = read_u16(bytes + 10),
        .left_edges = bytes[12],
        .right_edges = bytes[13]
    };
    return true;
}

bool wm_psvr2_pointer_normalize(double x, double y, double width, double height,
                                 uint16_t *normalized_x,
                                 uint16_t *normalized_y) {
    if (!normalized_x || !normalized_y || !isfinite(x) || !isfinite(y) ||
        !isfinite(width) || !isfinite(height) || width <= 0 || height <= 0 ||
        x < 0 || y < 0 || x >= width || y >= height) return false;
    double scaled_x = x / width * 65536.0;
    double scaled_y = y / height * 65536.0;
    *normalized_x = (uint16_t)(scaled_x >= 65535 ? 65535 : scaled_x);
    *normalized_y = (uint16_t)(scaled_y >= 65535 ? 65535 : scaled_y);
    return true;
}

void wm_psvr2_pointer_decoder_init(WmPsvr2PointerDecoder *decoder) {
    memset(decoder, 0, sizeof(*decoder));
}

static void enqueue(WmPsvr2PointerDecoder *decoder, WmEvent event) {
    unsigned index = (decoder->event_head + decoder->event_count) %
                     WM_PSVR2_POINTER_EVENTS;
    decoder->events[index] = event;
    decoder->event_count++;
}

static void cancel(WmPsvr2PointerDecoder *decoder) {
    if (decoder->visible || decoder->held_buttons) {
        enqueue(decoder, (WmEvent){
            .type = WM_EVENT_POINTER_LEAVE,
            .cancel_capture = true
        });
    }
    decoder->visible = false;
    decoder->held_buttons = 0;
    decoder->suppress_buttons = true;
}

static bool valid_edges(uint8_t old_edges, uint8_t new_edges,
                         bool old_down, bool new_down) {
    return (uint8_t)(new_edges - old_edges) == (old_down != new_down ? 1 : 0);
}

static void accept_state(WmPsvr2PointerDecoder *decoder,
                          WmPsvr2PointerState state, uint64_t now_ms) {
    bool new_session = !decoder->have_session ||
                       decoder->previous.session != state.session;
    if (!new_session) {
        uint16_t distance = (uint16_t)(state.sequence - decoder->previous.sequence);
        if (distance == 0 || distance >= 0x8000) {
            decoder->rejected_packets++;
            return;
        }
    }
    bool edges_valid = new_session ||
        (valid_edges(decoder->previous.left_edges, state.left_edges,
                     decoder->previous.buttons & WM_PSVR2_POINTER_LEFT,
                     state.buttons & WM_PSVR2_POINTER_LEFT) &&
         valid_edges(decoder->previous.right_edges, state.right_edges,
                     decoder->previous.buttons & WM_PSVR2_POINTER_RIGHT,
                     state.buttons & WM_PSVR2_POINTER_RIGHT));
    if (new_session || !edges_valid || (state.flags & WM_PSVR2_POINTER_RESET)) {
        cancel(decoder);
        if (!edges_valid) decoder->lost_button_edges++;
    }
    decoder->previous = state;
    decoder->have_session = true;
    decoder->last_packet_ms = now_ms;
    decoder->accepted_packets++;
    if (!(state.flags & WM_PSVR2_POINTER_ACTIVE)) {
        cancel(decoder);
        return;
    }
    if (!state.buttons) decoder->suppress_buttons = false;
    int x = (int)(((uint32_t)state.x * WM_FRAME_WIDTH) >> 16);
    int y = (int)(((uint32_t)state.y * WM_FRAME_HEIGHT) >> 16);
    /* Heartbeats do not flood the menu with identical MOVE events. */
    if (!decoder->visible || x != decoder->last_x || y != decoder->last_y) {
        enqueue(decoder, (WmEvent){ .type = WM_EVENT_POINTER_MOVE, .x = x, .y = y });
    }
    decoder->visible = true;
    /* Last applied position is separate from the queue, whose entries wrap. */
    decoder->last_x = x;
    decoder->last_y = y;
    uint8_t buttons = decoder->suppress_buttons ? 0 : state.buttons;
    for (unsigned bit = 0; bit < 2; bit++) {
        uint8_t mask = (uint8_t)(1 << bit);
        if ((decoder->held_buttons & mask) == (buttons & mask)) continue;
        enqueue(decoder, (WmEvent){
            .type = (buttons & mask) ? WM_EVENT_POINTER_DOWN : WM_EVENT_POINTER_UP,
            .x = x,
            .y = y,
            .button = bit == 0 ? WM_POINTER_LEFT : WM_POINTER_RIGHT
        });
    }
    decoder->held_buttons = buttons;
}

size_t wm_psvr2_pointer_decoder_feed(WmPsvr2PointerDecoder *decoder,
                                      const uint8_t *bytes, size_t size,
                                      uint64_t now_ms) {
    size_t consumed = 0;
    while (consumed < size && decoder->event_count <= WM_PSVR2_POINTER_EVENTS - 4) {
        decoder->bytes[decoder->byte_count++] = bytes[consumed++];
        if (decoder->byte_count < WM_PSVR2_POINTER_PACKET_SIZE) continue;
        WmPsvr2PointerState state;
        if (wm_psvr2_pointer_decode(decoder->bytes, &state)) {
            decoder->byte_count = 0;
            accept_state(decoder, state, now_ms);
        } else {
            decoder->rejected_packets++;
            memmove(decoder->bytes, decoder->bytes + 1,
                    WM_PSVR2_POINTER_PACKET_SIZE - 1);
            decoder->byte_count--;
        }
    }
    return consumed;
}

bool wm_psvr2_pointer_decoder_poll(WmPsvr2PointerDecoder *decoder,
                                    WmEvent *event) {
    if (!decoder->event_count) return false;
    *event = decoder->events[decoder->event_head];
    decoder->event_head = (decoder->event_head + 1) % WM_PSVR2_POINTER_EVENTS;
    decoder->event_count--;
    decoder->events_delivered++;
    if (event->type == WM_EVENT_POINTER_DOWN) {
        if (event->button == WM_POINTER_LEFT) decoder->left_down_events++;
        if (event->button == WM_POINTER_RIGHT) decoder->right_down_events++;
    } else if (event->type == WM_EVENT_POINTER_UP) {
        if (event->button == WM_POINTER_LEFT) decoder->left_up_events++;
        if (event->button == WM_POINTER_RIGHT) decoder->right_up_events++;
    }
    return true;
}

void wm_psvr2_pointer_decoder_tick(WmPsvr2PointerDecoder *decoder,
                                   uint64_t now_ms) {
    if (decoder->event_count < WM_PSVR2_POINTER_EVENTS &&
        decoder->have_session && now_ms - decoder->last_packet_ms >=
        WM_PSVR2_POINTER_WATCHDOG_MS) {
        if (decoder->visible || decoder->held_buttons)
            decoder->watchdog_cancels++;
        decoder->event_count = 0;
        decoder->event_head = 0;
        cancel(decoder);
    }
}

void wm_psvr2_pointer_decoder_disconnect(WmPsvr2PointerDecoder *decoder) {
    /* Buffered actions from a disconnected device must not execute later. */
    decoder->event_count = 0;
    decoder->event_head = 0;
    cancel(decoder);
    decoder->byte_count = 0;
    decoder->have_session = false;
}
