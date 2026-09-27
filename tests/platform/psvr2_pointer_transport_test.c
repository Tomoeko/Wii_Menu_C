#include "pointer_transport.h"

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

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

int main(void) {
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
    assert(transport.count == 4); /* Motion, down, motion, up: edges remain ordered. */
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
    assert(transport.suppressed_buttons & WM_PSVR2_POINTER_LEFT);
    close(pipe_fd[0]);
    puts("PSVR2 pointer coalescing, ordered transitions and backpressure checks passed.");
    return 0;
}
