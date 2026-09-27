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
    bool report_stats;
    uint64_t next_stats_ms;
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

static void report_input_stats(WmPsvr2Pointer *pointer, uint64_t now_ms) {
    if (!pointer->report_stats || now_ms < pointer->next_stats_ms) return;
    pointer->next_stats_ms = now_ms + 1000;
    const WmPsvr2PointerDecoder *decoder = &pointer->decoder;
    fprintf(stderr,
        "PSVR2 input stats: accepted=%llu rejected=%llu lost_edges=%llu "
        "watchdog=%llu events=%llu left_down/up=%llu/%llu "
        "right_down/up=%llu/%llu visible=%d held=%u\n",
        (unsigned long long)decoder->accepted_packets,
        (unsigned long long)decoder->rejected_packets,
        (unsigned long long)decoder->lost_button_edges,
        (unsigned long long)decoder->watchdog_cancels,
        (unsigned long long)decoder->events_delivered,
        (unsigned long long)decoder->left_down_events,
        (unsigned long long)decoder->left_up_events,
        (unsigned long long)decoder->right_down_events,
        (unsigned long long)decoder->right_up_events,
        decoder->visible, (unsigned)decoder->held_buttons);
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
    const char *stats = getenv("WM_PSVR2_INPUT_STATS");
    pointer->report_stats = stats && strcmp(stats, "1") == 0;
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
    report_input_stats(pointer, now_ms);
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
