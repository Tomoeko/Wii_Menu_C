#include "pointer_transport.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct TestWriter {
    uint8_t bytes[16384];
    size_t size;
    size_t budget;
    int error;
    bool return_zero;
} TestWriter;

static ssize_t limited_write(void *context, int fd, const void *bytes, size_t size) {
    (void)fd;
    TestWriter *writer = context;
    if (writer->error) {
        errno = writer->error;
        return -1;
    }
    if (writer->return_zero) return 0;
    if (!writer->budget) {
        errno = EAGAIN;
        return -1;
    }
    size_t count = size < 3 ? size : 3;
    if (count > writer->budget) count = writer->budget;
    assert(count <= sizeof(writer->bytes) - writer->size);
    memcpy(writer->bytes + writer->size, bytes, count);
    writer->size += count;
    writer->budget -= count;
    return (ssize_t)count;
}

static void test_transport_init(WmPointerTransport *transport, TestWriter *writer) {
    wm_pointer_transport_init(transport, "/unused", 1);
    transport->fd = open("/dev/null", O_WRONLY);
    assert(transport->fd >= 0);
    transport->state.flags = WM_PSVR2_POINTER_ACTIVE;
    transport->write_bytes = limited_write;
    transport->write_context = writer;
}

static void fill_pipe(int fd) {
    uint8_t bytes[1024] = {0};
    while (write(fd, bytes, sizeof(bytes)) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
}

static void drain_pipe(int fd) {
    uint8_t bytes[1024];
    while (read(fd, bytes, sizeof(bytes)) > 0) {}
    assert(errno == EAGAIN || errno == EWOULDBLOCK);
}

static void test_real_pipe_backpressure(void) {
    int pipe_fd[2];
    assert(pipe(pipe_fd) == 0);
    assert(fcntl(pipe_fd[0], F_SETFL, O_NONBLOCK) == 0);
    assert(fcntl(pipe_fd[1], F_SETFL, O_NONBLOCK) == 0);
    WmPointerTransport transport;
    wm_pointer_transport_init(&transport, "/unused", 1);
    transport.fd = pipe_fd[1];
    transport.state.flags = WM_PSVR2_POINTER_ACTIVE;
    fill_pipe(pipe_fd[1]);
    for (uint16_t x = 0; x < 20; x++) {
        wm_pointer_transport_update(&transport, x, 0, true, 0, 100);
    }
    assert(transport.count == 1 && transport.motions_coalesced == 19);
    wm_pointer_transport_update(&transport, 20, 0, true, WM_PSVR2_POINTER_LEFT, 100);
    for (uint16_t x = 21; x < 40; x++) {
        wm_pointer_transport_update(&transport, x, 0, true,
                                    WM_PSVR2_POINTER_LEFT, 100);
    }
    wm_pointer_transport_update(&transport, 40, 0, true, 0, 100);
    assert(transport.count == 4);
    drain_pipe(pipe_fd[0]);
    wm_pointer_transport_flush(&transport, 100);
    assert(transport.count == 0 && transport.packets_written == 4);
    uint8_t bytes[4 * WM_PSVR2_POINTER_PACKET_SIZE];
    assert(read(pipe_fd[0], bytes, sizeof(bytes)) == (ssize_t)sizeof(bytes));
    WmPsvr2PointerState state;
    assert(wm_psvr2_pointer_decode(bytes, &state) && state.x == 19 && !state.buttons);
    assert(wm_psvr2_pointer_decode(bytes + 16, &state) && state.x == 20 &&
           state.buttons == WM_PSVR2_POINTER_LEFT && state.left_edges == 1);
    assert(wm_psvr2_pointer_decode(bytes + 32, &state) && state.x == 39 &&
           state.buttons == WM_PSVR2_POINTER_LEFT && state.left_edges == 1);
    assert(wm_psvr2_pointer_decode(bytes + 48, &state) && state.x == 40 &&
           !state.buttons && state.left_edges == 2);
    fill_pipe(pipe_fd[1]);
    wm_pointer_transport_update(&transport, 40, 0, true, WM_PSVR2_POINTER_LEFT, 200);
    wm_pointer_transport_flush(&transport, 301);
    assert(transport.fd == -1 && transport.count == 0);
    assert(transport.last_disconnect_reason == WM_POINTER_DISCONNECT_WRITE_STALLED);
    assert(transport.suppressed_buttons & WM_PSVR2_POINTER_LEFT);
    close(pipe_fd[0]);
}

static void test_sustained_partial_progress(void) {
    TestWriter writer = {.budget = 3};
    WmPointerTransport transport;
    test_transport_init(&transport, &writer);
    wm_pointer_transport_update(&transport, 0, 0, true, 0, 100);
    assert(transport.offset == 3);
    wm_pointer_transport_update(&transport, 0, 0, true, WM_PSVR2_POINTER_LEFT, 100);
    uint8_t buttons = WM_PSVR2_POINTER_LEFT;
    /* A continuously nonempty queue at 125 Hz must survive many original
     * 100 ms deadlines while short writes advance its actual oldest packet. */
    for (uint16_t i = 1; i <= 400; i++) {
        if (i == 2 || i % 50 == 11) {
            buttons = wm_pointer_event_buttons(buttons, WM_EVENT_POINTER_UP, WM_POINTER_LEFT);
        } else if (i % 50 == 10) {
            buttons = wm_pointer_event_buttons(buttons, WM_EVENT_POINTER_DOWN, WM_POINTER_LEFT);
        }
        if (i % 50 == 20) {
            buttons = wm_pointer_event_buttons(buttons, WM_EVENT_POINTER_DOWN, WM_POINTER_RIGHT);
        } else if (i % 50 == 21) {
            buttons = wm_pointer_event_buttons(buttons, WM_EVENT_POINTER_UP, WM_POINTER_RIGHT);
        }
        writer.budget = WM_PSVR2_POINTER_PACKET_SIZE;
        wm_pointer_transport_update(&transport, i, i, true, buttons, 100 + (uint64_t)i * 8);
        assert(transport.fd >= 0 && transport.count > 0 && transport.offset == 3);
        assert(transport.oldest_pending_ms >= 100 + (uint64_t)i * 8 - 16);
    }
    writer.budget = sizeof(writer.bytes);
    wm_pointer_transport_flush(&transport, 3300);
    assert(!transport.count && !transport.disconnects);
    assert(writer.size % WM_PSVR2_POINTER_PACKET_SIZE == 0);
    uint8_t previous_buttons = 0;
    uint8_t left_edges = 0;
    uint8_t right_edges = 0;
    uint16_t previous_sequence = 0;
    for (size_t i = 0; i < writer.size; i += WM_PSVR2_POINTER_PACKET_SIZE) {
        WmPsvr2PointerState state;
        assert(wm_psvr2_pointer_decode(writer.bytes + i, &state));
        assert(state.flags == WM_PSVR2_POINTER_ACTIVE);
        assert((uint16_t)(state.sequence - previous_sequence) > 0);
        if ((state.buttons ^ previous_buttons) & WM_PSVR2_POINTER_LEFT) left_edges++;
        if ((state.buttons ^ previous_buttons) & WM_PSVR2_POINTER_RIGHT) right_edges++;
        assert(state.left_edges == left_edges && state.right_edges == right_edges);
        previous_buttons = state.buttons;
        previous_sequence = state.sequence;
    }
    assert(left_edges == 18 && right_edges == 16 && !previous_buttons);
    assert(transport.packets_written >= 400 && transport.would_block_count >= 400);
    wm_pointer_transport_close(&transport);
}

static void test_stalled_coalescing(void) {
    TestWriter writer = {0};
    WmPointerTransport transport;
    test_transport_init(&transport, &writer);
    for (uint64_t now = 100; now < 201; now += 8) {
        wm_pointer_transport_update(&transport, (uint16_t)now, 0, true, 0, now);
        assert(transport.fd >= 0 && transport.count == 1);
    }
    wm_pointer_transport_update(&transport, 201, 0, true, 0, 201);
    assert(transport.fd == -1 && transport.disconnects == 1);
    assert(transport.last_disconnect_reason == WM_POINTER_DISCONNECT_WRITE_STALLED);
    assert(!transport.last_error && transport.last_progress_ms == 100);
}

static void test_stale_partial_packet(void) {
    TestWriter writer = {.budget = 3};
    WmPointerTransport transport;
    test_transport_init(&transport, &writer);
    wm_pointer_transport_update(&transport, 0, 0, true, 0, 100);
    writer.budget = 1;
    wm_pointer_transport_flush(&transport, 140);
    writer.budget = 1;
    wm_pointer_transport_flush(&transport, 180);
    assert(transport.offset == 5 && transport.last_progress_ms == 180);
    writer.budget = 1;
    wm_pointer_transport_flush(&transport, 201);
    assert(transport.fd == -1 && transport.disconnects == 1);
    assert(transport.last_disconnect_reason == WM_POINTER_DISCONNECT_QUEUE_AGE);
    assert(writer.size == 5);
}

static void test_write_failures(void) {
    for (unsigned zero = 0; zero < 2; zero++) {
        TestWriter writer = {.error = zero ? 0 : EIO, .return_zero = zero != 0};
        WmPointerTransport transport;
        test_transport_init(&transport, &writer);
        wm_pointer_transport_update(&transport, 0, 0, true, WM_PSVR2_POINTER_LEFT, 100);
        assert(transport.fd == -1 && transport.disconnects == 1);
        assert(transport.last_disconnect_reason == (zero ?
            WM_POINTER_DISCONNECT_WRITE_ZERO : WM_POINTER_DISCONNECT_WRITE_ERROR));
        assert(transport.last_error == (zero ? 0 : EIO));
        assert(transport.suppressed_buttons == WM_PSVR2_POINTER_LEFT);
        assert(transport.retry_ms == 1100);
    }
}

static void test_full_queue(void) {
    TestWriter writer = {0};
    WmPointerTransport transport;
    test_transport_init(&transport, &writer);
    for (unsigned i = 0; i < WM_POINTER_PENDING_PACKETS - 1; i++) {
        wm_pointer_transport_update(&transport, 0, 0, true,
            i % 2 ? 0 : WM_PSVR2_POINTER_LEFT, 100);
    }
    wm_pointer_transport_update(&transport, 1, 0, true, WM_PSVR2_POINTER_LEFT, 100);
    assert(transport.count == WM_POINTER_PENDING_PACKETS);
    wm_pointer_transport_update(&transport, 2, 0, true, WM_PSVR2_POINTER_LEFT, 100);
    assert(transport.fd >= 0 && transport.count == WM_POINTER_PENDING_PACKETS);
    assert(transport.motions_coalesced == 1);
    wm_pointer_transport_update(&transport, 3, 0, true, 0, 100);
    assert(transport.fd == -1 && transport.disconnects == 1);
    assert(transport.last_disconnect_reason == WM_POINTER_DISCONNECT_QUEUE_FULL);
}

static void test_delayed_click_events(void) {
    TestWriter writer = {0};
    WmPointerTransport transport;
    test_transport_init(&transport, &writer);
    /* Both events may be dispatched after the physical button has already
     * been released. Replaying event transitions must still preserve a click. */
    uint8_t buttons = wm_pointer_event_buttons(0, WM_EVENT_POINTER_DOWN, WM_POINTER_LEFT);
    wm_pointer_transport_update(&transport, 42, 17, true, buttons, 100);
    buttons = wm_pointer_event_buttons(buttons, WM_EVENT_POINTER_UP, WM_POINTER_LEFT);
    wm_pointer_transport_update(&transport, 42, 17, true, buttons, 100);
    assert(transport.count == 2);
    writer.budget = sizeof(writer.bytes);
    wm_pointer_transport_flush(&transport, 101);
    WmPsvr2PointerState state;
    assert(writer.size == 2 * WM_PSVR2_POINTER_PACKET_SIZE);
    assert(wm_psvr2_pointer_decode(writer.bytes, &state));
    assert(state.buttons == WM_PSVR2_POINTER_LEFT && state.left_edges == 1);
    assert(wm_psvr2_pointer_decode(writer.bytes + WM_PSVR2_POINTER_PACKET_SIZE, &state));
    assert(!state.buttons && state.left_edges == 2);
    wm_pointer_transport_close(&transport);
}

int main(void) {
    test_real_pipe_backpressure();
    test_sustained_partial_progress();
    test_stalled_coalescing();
    test_stale_partial_packet();
    test_write_failures();
    test_full_queue();
    test_delayed_click_events();
    puts("PSVR2 pointer sustained short writes, ordered clicks and transport failure checks passed.");
    return 0;
}
