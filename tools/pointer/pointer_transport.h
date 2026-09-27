#ifndef WM_POINTER_TRANSPORT_H
#define WM_POINTER_TRANSPORT_H

#include "wii_menu/platform/psvr2_pointer_protocol.h"

#include <sys/types.h>

enum { WM_POINTER_PENDING_PACKETS = 128 };

typedef struct WmPointerPacket {
    uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE];
    bool transition;
    uint64_t queued_ms;
} WmPointerPacket;

typedef enum WmPointerDisconnectReason {
    WM_POINTER_DISCONNECT_NONE,
    WM_POINTER_DISCONNECT_WRITE_ERROR,
    WM_POINTER_DISCONNECT_WRITE_ZERO,
    WM_POINTER_DISCONNECT_QUEUE_FULL,
    WM_POINTER_DISCONNECT_QUEUE_AGE,
    WM_POINTER_DISCONNECT_WRITE_STALLED
} WmPointerDisconnectReason;

typedef ssize_t (*WmPointerWrite)(void *context, int fd,
                                  const void *bytes, size_t size);

typedef struct WmPointerTransport {
    int fd;
    const char *path; /* Caller owned. */
    WmPsvr2PointerState state;
    WmPointerPacket pending[WM_POINTER_PENDING_PACKETS];
    unsigned begin;
    unsigned count;
    unsigned offset;
    uint8_t physical_buttons;
    uint8_t suppressed_buttons;
    uint64_t retry_ms;
    uint64_t oldest_pending_ms;
    uint64_t last_progress_ms;
    uint64_t connection_generation;
    uint64_t packets_written;
    uint64_t bytes_written;
    uint64_t would_block_count;
    uint64_t motions_coalesced;
    uint64_t disconnects;
    WmPointerDisconnectReason last_disconnect_reason;
    int last_error;
    /* NULL selects write(2). An adapter allows deterministic short-write and
     * fault checks without depending on a serial driver or private device. */
    WmPointerWrite write_bytes;
    void *write_context;
} WmPointerTransport;

void wm_pointer_transport_init(WmPointerTransport *transport,
                                const char *path, uint16_t initial_session);
void wm_pointer_transport_close(WmPointerTransport *transport);
/* Main/event thread only. All writes are nonblocking, with no allocation or
 * subprocess. Motion may replace only an unsent motion snapshot; transitions
 * are ordered. Queue overflow, stale packets or >100ms without write progress
 * disconnect safely. */
void wm_pointer_transport_update(WmPointerTransport *transport,
                                  uint16_t x, uint16_t y, bool active,
                                  uint8_t physical_buttons, uint64_t now_ms);
void wm_pointer_transport_heartbeat(WmPointerTransport *transport,
                                     uint64_t now_ms);
void wm_pointer_transport_flush(WmPointerTransport *transport, uint64_t now_ms);
const char *wm_pointer_disconnect_reason(WmPointerDisconnectReason reason);
/* Replay the local queued event, independent of the physical button snapshot
 * at the later time when the application processes that event. */
uint8_t wm_pointer_event_buttons(uint8_t buttons, WmEventType type,
                                  WmPointerButton button);

#endif
