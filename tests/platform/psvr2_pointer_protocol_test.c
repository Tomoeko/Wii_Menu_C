#include "wii_menu/platform/psvr2_pointer_protocol.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void feed(WmPsvr2PointerDecoder *decoder, WmPsvr2PointerState state,
                   uint64_t now_ms) {
    uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE];
    wm_psvr2_pointer_encode(bytes, &state);
    assert(wm_psvr2_pointer_decoder_feed(decoder, bytes, sizeof(bytes), now_ms) ==
           sizeof(bytes));
}

static WmEvent event(WmPsvr2PointerDecoder *decoder, WmEventType type) {
    WmEvent result;
    assert(wm_psvr2_pointer_decoder_poll(decoder, &result));
    assert(result.type == type);
    return result;
}

static void empty(WmPsvr2PointerDecoder *decoder) {
    WmEvent unused;
    assert(!wm_psvr2_pointer_decoder_poll(decoder, &unused));
}

static void test_mapping(void) {
    uint16_t x = 0, y = 0;
    assert(wm_psvr2_pointer_normalize(480, 270, 960, 540, &x, &y));
    assert(x == 32768 && y == 32768);
    assert(wm_psvr2_pointer_normalize(200, 500, 400, 1000, &x, &y));
    assert(x == 32768 && y == 32768);
    assert(wm_psvr2_pointer_normalize(0, 0, 640, 456, &x, &y));
    assert(x == 0 && y == 0);
    assert(wm_psvr2_pointer_normalize(639.999, 455.999, 640, 456, &x, &y));
    assert(x == 65535 && y == 65535);
    assert(!wm_psvr2_pointer_normalize(-1, 0, 960, 540, &x, &y));
    assert(!wm_psvr2_pointer_normalize(960, 0, 960, 540, &x, &y));
    assert(!wm_psvr2_pointer_normalize(0, 540, 960, 540, &x, &y));
    assert(!wm_psvr2_pointer_normalize(NAN, 0, 960, 540, &x, &y));
    assert(!wm_psvr2_pointer_normalize(0, 0, 0, 540, &x, &y));
}

static void test_framing(void) {
    WmPsvr2PointerState state = {
        .session = 0x1234, .sequence = 1,
        .flags = WM_PSVR2_POINTER_ACTIVE, .x = 32768, .y = 32768
    };
    uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE];
    wm_psvr2_pointer_encode(bytes, &state);
    assert(bytes[0] == 'C' && bytes[1] == 'T' && bytes[4] == 0x34 && bytes[5] == 0x12);
    WmPsvr2PointerState decoded;
    assert(wm_psvr2_pointer_decode(bytes, &decoded));
    assert(decoded.session == state.session && decoded.x == state.x);
    for (unsigned bit = 0; bit < sizeof(bytes) * 8; bit++) {
        bytes[bit / 8] ^= (uint8_t)(1 << (bit % 8));
        assert(!wm_psvr2_pointer_decode(bytes, &decoded));
        bytes[bit / 8] ^= (uint8_t)(1 << (bit % 8));
    }
    for (size_t split = 0; split <= sizeof(bytes); split++) {
        WmPsvr2PointerDecoder decoder;
        wm_psvr2_pointer_decoder_init(&decoder);
        assert(wm_psvr2_pointer_decoder_feed(&decoder, bytes, split, 1) == split);
        assert(wm_psvr2_pointer_decoder_feed(&decoder, bytes + split,
                                              sizeof(bytes) - split, 1) ==
               sizeof(bytes) - split);
        WmEvent moved = event(&decoder, WM_EVENT_POINTER_MOVE);
        assert(moved.x == 320 && moved.y == 228);
        empty(&decoder);
    }
    WmPsvr2PointerDecoder decoder;
    wm_psvr2_pointer_decoder_init(&decoder);
    uint8_t noise[] = {0, 'C', 'T', 0, 'C', 0, 'T'};
    assert(wm_psvr2_pointer_decoder_feed(&decoder, noise, sizeof(noise), 1) ==
           sizeof(noise));
    feed(&decoder, state, 2);
    event(&decoder, WM_EVENT_POINTER_MOVE);
    empty(&decoder);
}

static void test_buttons_and_loss(void) {
    WmPsvr2PointerDecoder decoder;
    wm_psvr2_pointer_decoder_init(&decoder);
    WmPsvr2PointerState state = {
        .session = 1, .sequence = 65535,
        .flags = WM_PSVR2_POINTER_ACTIVE, .x = 65535, .y = 65535
    };
    feed(&decoder, state, 1);
    WmEvent moved = event(&decoder, WM_EVENT_POINTER_MOVE);
    assert(moved.x == 639 && moved.y == 455);
    state.sequence++; /* Sequence wraps without rejecting a valid frame. */
    state.buttons = WM_PSVR2_POINTER_LEFT | WM_PSVR2_POINTER_RIGHT;
    state.left_edges++;
    state.right_edges++;
    feed(&decoder, state, 2);
    assert(event(&decoder, WM_EVENT_POINTER_DOWN).button == WM_POINTER_LEFT);
    assert(event(&decoder, WM_EVENT_POINTER_DOWN).button == WM_POINTER_RIGHT);
    feed(&decoder, state, 3); /* Duplicate frame must not repeat buttons. */
    empty(&decoder);
    assert(decoder.last_packet_ms == 2);
    state.sequence++;
    state.buttons = 0;
    state.left_edges++;
    state.right_edges++;
    feed(&decoder, state, 4);
    assert(event(&decoder, WM_EVENT_POINTER_UP).button == WM_POINTER_LEFT);
    assert(event(&decoder, WM_EVENT_POINTER_UP).button == WM_POINTER_RIGHT);
    state.sequence += 3; /* A dropped down/up pair is detectable in edge counters. */
    state.left_edges += 2;
    feed(&decoder, state, 5);
    assert(event(&decoder, WM_EVENT_POINTER_LEAVE).cancel_capture);
    event(&decoder, WM_EVENT_POINTER_MOVE);
    empty(&decoder);
    assert(decoder.lost_button_edges == 1);
    state.sequence++;
    state.buttons = WM_PSVR2_POINTER_LEFT;
    state.left_edges++;
    feed(&decoder, state, 6);
    event(&decoder, WM_EVENT_POINTER_DOWN);
    wm_psvr2_pointer_decoder_tick(&decoder, 255);
    empty(&decoder);
    wm_psvr2_pointer_decoder_tick(&decoder, 256);
    assert(event(&decoder, WM_EVENT_POINTER_LEAVE).cancel_capture);
    state.sequence++;
    feed(&decoder, state, 257); /* A held button cannot re-trigger after timeout. */
    event(&decoder, WM_EVENT_POINTER_MOVE);
    empty(&decoder);
    state.sequence++;
    state.buttons = 0;
    state.left_edges++;
    feed(&decoder, state, 258);
    empty(&decoder);
    state.sequence++;
    state.buttons = WM_PSVR2_POINTER_LEFT;
    state.left_edges++;
    feed(&decoder, state, 259);
    event(&decoder, WM_EVENT_POINTER_DOWN);
    wm_psvr2_pointer_decoder_disconnect(&decoder);
    assert(event(&decoder, WM_EVENT_POINTER_LEAVE).cancel_capture);
    state.session++;
    state.sequence = 1;
    feed(&decoder, state, 260); /* A new connection's held button is suppressed. */
    event(&decoder, WM_EVENT_POINTER_MOVE);
    empty(&decoder);
}

static void test_bounded_event_queue(void) {
    uint8_t stream[100 * WM_PSVR2_POINTER_PACKET_SIZE];
    WmPsvr2PointerState state = {
        .session = 4, .flags = WM_PSVR2_POINTER_ACTIVE
    };
    for (unsigned index = 0; index < 100; index++) {
        state.sequence++;
        if (index) {
            state.buttons ^= WM_PSVR2_POINTER_LEFT;
            state.left_edges++;
        }
        wm_psvr2_pointer_encode(stream + index * WM_PSVR2_POINTER_PACKET_SIZE, &state);
    }
    WmPsvr2PointerDecoder decoder;
    wm_psvr2_pointer_decoder_init(&decoder);
    size_t consumed = 0;
    unsigned down = 0, up = 0;
    while (consumed < sizeof(stream) || decoder.event_count) {
        consumed += wm_psvr2_pointer_decoder_feed(
            &decoder, stream + consumed, sizeof(stream) - consumed, 1);
        WmEvent next;
        while (wm_psvr2_pointer_decoder_poll(&decoder, &next)) {
            down += next.type == WM_EVENT_POINTER_DOWN;
            up += next.type == WM_EVENT_POINTER_UP;
        }
    }
    assert(down == 50 && up == 49);
    assert(decoder.lost_button_edges == 0);
}

int main(void) {
    test_mapping();
    test_framing();
    test_buttons_and_loss();
    test_bounded_event_queue();
    puts("PSVR2 pointer framing, mapping, transitions and failsafe checks passed.");
    return 0;
}
