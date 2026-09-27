#ifndef WM_POINTER_TRANSPORT_H
#define WM_POINTER_TRANSPORT_H

#include "wii_menu/platform/psvr2_pointer_protocol.h"

enum { WM_POINTER_PENDING_PACKETS = 128 };

typedef struct WmPointerPacket {
    uint8_t bytes[WM_PSVR2_POINTER_PACKET_SIZE];
    bool transition;
} WmPointerPacket;

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
    uint64_t packets_written;
    uint64_t motions_coalesced;
    uint64_t disconnects;
} WmPointerTransport;

void wm_pointer_transport_init(WmPointerTransport *transport,
                                const char *path, uint16_t initial_session);
void wm_pointer_transport_close(WmPointerTransport *transport);
/* Main/event thread only. All writes are nonblocking, with no allocation or
 * subprocess. Motion may replace only an unsent motion snapshot; transitions
 * are ordered. Queue overflow or >100ms backpressure disconnects safely. */
void wm_pointer_transport_update(WmPointerTransport *transport,
                                  uint16_t x, uint16_t y, bool active,
                                  uint8_t physical_buttons, uint64_t now_ms);
void wm_pointer_transport_heartbeat(WmPointerTransport *transport,
                                     uint64_t now_ms);
void wm_pointer_transport_flush(WmPointerTransport *transport, uint64_t now_ms);

#endif
