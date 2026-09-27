#define _POSIX_C_SOURCE 200809L
#include "wii_menu/platform/psvr2_pointer.h"
#include "wii_menu/platform/psvr2_pointer_protocol.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

struct WmPsvr2Pointer {
    int fd;
    char *path;
    WmPsvr2PointerDecoder decoder;
    uint8_t buffer[1024]; /* Stage3 fast_input requires a complete USB buffer. */
    size_t begin;
    size_t end;
    uint64_t retry_ms;
};

static uint64_t monotonic_ms(void) {
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) return 0;
    return (uint64_t)now.tv_sec * 1000 + (uint64_t)now.tv_nsec / 1000000;
}

static void disconnect_input(WmPsvr2Pointer *pointer, uint64_t now_ms) {
    if (pointer->fd >= 0) close(pointer->fd);
    pointer->fd = -1;
    pointer->begin = pointer->end = 0;
    pointer->retry_ms = now_ms + 1000;
    wm_psvr2_pointer_decoder_disconnect(&pointer->decoder);
}

WmPsvr2Pointer *wm_psvr2_pointer_create(const char *path) {
    WmPsvr2Pointer *pointer = calloc(1, sizeof(*pointer));
    if (!pointer) return NULL;
    pointer->fd = -1;
    pointer->path = strdup(path ? path : "/dev/fast_input");
    if (!pointer->path) {
        free(pointer);
        return NULL;
    }
    wm_psvr2_pointer_decoder_init(&pointer->decoder);
    return pointer;
}

void wm_psvr2_pointer_destroy(WmPsvr2Pointer *pointer) {
    if (!pointer) return;
    if (pointer->fd >= 0) close(pointer->fd);
    free(pointer->path);
    free(pointer);
}

bool wm_psvr2_pointer_poll(WmPsvr2Pointer *pointer, WmEvent *event) {
    if (!pointer || !event) return false;
    uint64_t now_ms = monotonic_ms();
    wm_psvr2_pointer_decoder_tick(&pointer->decoder, now_ms);
    if (wm_psvr2_pointer_decoder_poll(&pointer->decoder, event)) return true;
    if (pointer->fd < 0 && now_ms >= pointer->retry_ms) {
        pointer->fd = open(pointer->path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        pointer->retry_ms = now_ms + 1000;
        if (pointer->fd >= 0) fprintf(stderr, "PSVR2 pointer input connected.\n");
    }
    /* Bound work per poll. Leave residue in place while queued transitions are
     * consumed by the menu; no event queue is silently overwritten. */
    for (unsigned iteration = 0; iteration < 4 && pointer->fd >= 0; iteration++) {
        if (pointer->begin == pointer->end) {
            ssize_t count = read(pointer->fd, pointer->buffer,
                                 sizeof(pointer->buffer));
            if (count < 0 && errno == EINTR) continue;
            if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            if (count <= 0) {
                fprintf(stderr, "PSVR2 pointer input disconnected.\n");
                disconnect_input(pointer, now_ms);
                break;
            }
            pointer->begin = 0;
            pointer->end = (size_t)count;
        }
        pointer->begin += wm_psvr2_pointer_decoder_feed(
            &pointer->decoder, pointer->buffer + pointer->begin,
            pointer->end - pointer->begin, now_ms);
        if (wm_psvr2_pointer_decoder_poll(&pointer->decoder, event)) return true;
    }
    return wm_psvr2_pointer_decoder_poll(&pointer->decoder, event);
}
