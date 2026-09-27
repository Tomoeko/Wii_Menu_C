#define _DEFAULT_SOURCE
#include "pointer_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

static void disconnect_transport(WmPointerTransport *transport, uint64_t now_ms) {
    if (transport->fd >= 0) {
        close(transport->fd);
        transport->disconnects++;
        fprintf(stderr, "Pointer transport disconnected; target watchdog cancels input.\n");
    }
    transport->fd = -1;
    transport->begin = transport->count = transport->offset = 0;
    transport->suppressed_buttons |= transport->physical_buttons;
    transport->state.buttons = 0;
    transport->retry_ms = now_ms + 1000;
}

void wm_pointer_transport_init(WmPointerTransport *transport,
                                const char *path, uint16_t initial_session) {
    memset(transport, 0, sizeof(*transport));
    transport->fd = -1;
    transport->path = path;
    transport->state.session = initial_session;
}

void wm_pointer_transport_close(WmPointerTransport *transport) {
    if (transport->fd >= 0) close(transport->fd);
    transport->fd = -1;
    transport->count = 0;
}

static bool connect_transport(WmPointerTransport *transport, uint64_t now_ms) {
    if (transport->fd >= 0) return true;
    if (now_ms < transport->retry_ms) return false;
    transport->retry_ms = now_ms + 1000;
    int fd = open(transport->path, O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) return false;
    struct termios settings;
    if (ioctl(fd, TIOCEXCL) != 0 || tcgetattr(fd, &settings) != 0) {
        close(fd);
        return false;
    }
    cfmakeraw(&settings);
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cflag &= ~(tcflag_t)(PARENB | CSTOPB | CSIZE);
    settings.c_cflag |= CS8;
#ifdef CRTSCTS
    settings.c_cflag &= ~(tcflag_t)CRTSCTS;
#endif
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if (cfsetispeed(&settings, B115200) != 0 ||
        cfsetospeed(&settings, B115200) != 0 ||
        tcsetattr(fd, TCSANOW, &settings) != 0) {
        close(fd);
        return false;
    }
    transport->fd = fd;
    transport->state.session++;
    transport->state.sequence = 0;
    transport->state.left_edges = transport->state.right_edges = 0;
    transport->state.buttons = 0;
    transport->suppressed_buttons |= transport->physical_buttons;
    fprintf(stderr, "Pointer connected on the explicitly selected dedicated input port.\n");
    return true;
}

static void queue_state(WmPointerTransport *transport, bool transition,
                         uint64_t now_ms) {
    if (transport->count == WM_POINTER_PENDING_PACKETS) {
        disconnect_transport(transport, now_ms);
        return;
    }
    unsigned index = (transport->begin + transport->count) %
                     WM_POINTER_PENDING_PACKETS;
    if (!transition && transport->count > 0) {
        unsigned last = (index + WM_POINTER_PENDING_PACKETS - 1) %
                        WM_POINTER_PENDING_PACKETS;
        if (!transport->pending[last].transition &&
            (transport->count > 1 || transport->offset == 0)) {
            index = last;
            transport->motions_coalesced++;
        } else {
            transport->count++;
        }
    } else {
        transport->count++;
    }
    if (transport->count == 1 && transport->offset == 0) {
        transport->oldest_pending_ms = now_ms;
    }
    transport->state.sequence++;
    wm_psvr2_pointer_encode(transport->pending[index].bytes, &transport->state);
    transport->pending[index].transition = transition;
}

void wm_pointer_transport_flush(WmPointerTransport *transport, uint64_t now_ms) {
    if (transport->fd < 0) return;
    if (transport->count && now_ms - transport->oldest_pending_ms > 100) {
        disconnect_transport(transport, now_ms);
        return;
    }
    while (transport->count) {
        WmPointerPacket *packet = &transport->pending[transport->begin];
        ssize_t count = write(transport->fd, packet->bytes + transport->offset,
                             WM_PSVR2_POINTER_PACKET_SIZE - transport->offset);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;
        if (count <= 0) {
            disconnect_transport(transport, now_ms);
            return;
        }
        transport->offset += (unsigned)count;
        if (transport->offset == WM_PSVR2_POINTER_PACKET_SIZE) {
            transport->offset = 0;
            transport->begin = (transport->begin + 1) % WM_POINTER_PENDING_PACKETS;
            transport->count--;
            transport->packets_written++;
        }
    }
}

void wm_pointer_transport_update(WmPointerTransport *transport,
                                  uint16_t x, uint16_t y, bool active,
                                  uint8_t physical_buttons, uint64_t now_ms) {
    transport->physical_buttons = physical_buttons & 3;
    transport->suppressed_buttons &= transport->physical_buttons;
    if (!active) transport->suppressed_buttons |= transport->physical_buttons;
    if (!connect_transport(transport, now_ms)) return;
    uint8_t buttons = active ? transport->physical_buttons &
                      (uint8_t)~transport->suppressed_buttons : 0;
    bool transition = buttons != transport->state.buttons ||
                      active != !!(transport->state.flags & WM_PSVR2_POINTER_ACTIVE);
    if ((buttons ^ transport->state.buttons) & WM_PSVR2_POINTER_LEFT) {
        transport->state.left_edges++;
    }
    if ((buttons ^ transport->state.buttons) & WM_PSVR2_POINTER_RIGHT) {
        transport->state.right_edges++;
    }
    transport->state.buttons = buttons;
    transport->state.flags = active ? WM_PSVR2_POINTER_ACTIVE : WM_PSVR2_POINTER_RESET;
    transport->state.x = x;
    transport->state.y = y;
    queue_state(transport, transition, now_ms);
    wm_pointer_transport_flush(transport, now_ms);
}

void wm_pointer_transport_heartbeat(WmPointerTransport *transport,
                                     uint64_t now_ms) {
    if (!connect_transport(transport, now_ms)) return;
    queue_state(transport, false, now_ms);
    wm_pointer_transport_flush(transport, now_ms);
}
