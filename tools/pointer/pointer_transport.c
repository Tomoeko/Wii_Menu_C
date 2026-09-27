#define _DEFAULT_SOURCE
#include "pointer_transport.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

const char *wm_pointer_disconnect_reason(WmPointerDisconnectReason reason) {
    switch (reason) {
        case WM_POINTER_DISCONNECT_WRITE_ERROR: return "write failed";
        case WM_POINTER_DISCONNECT_WRITE_ZERO: return "write returned zero";
        case WM_POINTER_DISCONNECT_QUEUE_FULL: return "transition queue full";
        case WM_POINTER_DISCONNECT_QUEUE_AGE: return "oldest packet exceeded 100 ms";
        case WM_POINTER_DISCONNECT_WRITE_STALLED: return "no write progress for 100 ms";
        default: return "none";
    }
}

static uint64_t age_ms(uint64_t now_ms, uint64_t earlier_ms) {
    return now_ms >= earlier_ms ? now_ms - earlier_ms : 0;
}

uint8_t wm_pointer_event_buttons(uint8_t buttons, WmEventType type,
                                  WmPointerButton button) {
    uint8_t mask = button == WM_POINTER_LEFT ? WM_PSVR2_POINTER_LEFT :
                   button == WM_POINTER_RIGHT ? WM_PSVR2_POINTER_RIGHT : 0;
    if (type == WM_EVENT_POINTER_DOWN) return buttons | mask;
    if (type == WM_EVENT_POINTER_UP) return buttons & (uint8_t)~mask;
    return buttons;
}

static void disconnect_transport(WmPointerTransport *transport, uint64_t now_ms,
                                   WmPointerDisconnectReason reason,
                                   int error_code) {
    transport->last_disconnect_reason = reason;
    transport->last_error = error_code;
    if (transport->fd >= 0) {
        fprintf(stderr, "Pointer disconnected: %s; errno=%d (%s); "
                "pending=%u partial=%u oldest=%llu ms no_progress=%llu ms "
                "bytes=%llu packets=%llu would_block=%llu.\n",
                wm_pointer_disconnect_reason(reason), error_code,
                error_code ? strerror(error_code) : "none", transport->count,
                transport->offset,
                (unsigned long long)(transport->count ?
                    age_ms(now_ms, transport->oldest_pending_ms) : 0),
                (unsigned long long)age_ms(now_ms, transport->last_progress_ms),
                (unsigned long long)transport->bytes_written,
                (unsigned long long)transport->packets_written,
                (unsigned long long)transport->would_block_count);
        close(transport->fd);
        transport->disconnects++;
    }
    transport->fd = -1;
    transport->begin = transport->count = transport->offset = 0;
    transport->suppressed_buttons |= transport->physical_buttons;
    transport->state.buttons = 0;
    transport->retry_ms = now_ms + 1000;
}

static bool connect_error(int fd, const char *operation) {
    int error_code = errno;
    if (fd >= 0) close(fd);
    fprintf(stderr, "Pointer connect failed (%s): errno=%d (%s).\n",
            operation, error_code, strerror(error_code));
    return false;
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
    if (fd < 0) return connect_error(-1, "open");
    struct termios settings;
    if (ioctl(fd, TIOCEXCL) != 0) return connect_error(fd, "exclusive serial ownership");
    if (tcgetattr(fd, &settings) != 0) return connect_error(fd, "read termios");
    tcflag_t inherited_cflag = settings.c_cflag;
    tcflag_t inherited_iflag = settings.c_iflag;
    cfmakeraw(&settings);
    settings.c_cflag |= CLOCAL | CREAD;
    settings.c_cflag &= ~(tcflag_t)(PARENB | CSTOPB | CSIZE);
    settings.c_cflag |= CS8;
#ifdef CRTSCTS
    settings.c_cflag &= ~(tcflag_t)CRTSCTS;
#endif
#ifdef CDTR_IFLOW
    settings.c_cflag &= ~(tcflag_t)CDTR_IFLOW;
#endif
#ifdef CDSR_OFLOW
    settings.c_cflag &= ~(tcflag_t)CDSR_OFLOW;
#endif
#ifdef CCAR_OFLOW
    settings.c_cflag &= ~(tcflag_t)CCAR_OFLOW;
#endif
    settings.c_iflag &= ~(tcflag_t)(IXON | IXOFF | IXANY);
    settings.c_cc[VMIN] = 0;
    settings.c_cc[VTIME] = 0;
    if (cfsetispeed(&settings, B115200) != 0 ||
        cfsetospeed(&settings, B115200) != 0 ||
        tcsetattr(fd, TCSANOW, &settings) != 0) {
        return connect_error(fd, "configure raw serial");
    }
    int modem_bits = TIOCM_DTR | TIOCM_RTS;
    bool modem_lines_set = ioctl(fd, TIOCMBIS, &modem_bits) == 0;
    if (!modem_lines_set && errno != ENOTTY && errno != EINVAL) {
        return connect_error(fd, "assert DTR/RTS");
    }
    transport->fd = fd;
    transport->connection_generation++;
    transport->state.session++;
    transport->state.sequence = 0;
    transport->state.left_edges = transport->state.right_edges = 0;
    transport->state.buttons = 0;
    transport->suppressed_buttons |= transport->physical_buttons;
    transport->last_progress_ms = now_ms;
    fprintf(stderr, "Pointer connected: session=%u generation=%llu; "
            "cflag=0x%lx->0x%lx iflag=0x%lx->0x%lx; DTR/RTS %s.\n",
            transport->state.session,
            (unsigned long long)transport->connection_generation,
            (unsigned long)inherited_cflag, (unsigned long)settings.c_cflag,
            (unsigned long)inherited_iflag, (unsigned long)settings.c_iflag,
            modem_lines_set ? "asserted" : "unsupported by descriptor");
    return true;
}

static void queue_state(WmPointerTransport *transport, bool transition,
                         uint64_t now_ms) {
    bool was_empty = transport->count == 0;
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
            if (transport->count == WM_POINTER_PENDING_PACKETS) {
                disconnect_transport(transport, now_ms, WM_POINTER_DISCONNECT_QUEUE_FULL, 0);
                return;
            }
            transport->count++;
        }
    } else {
        if (transport->count == WM_POINTER_PENDING_PACKETS) {
            disconnect_transport(transport, now_ms, WM_POINTER_DISCONNECT_QUEUE_FULL, 0);
            return;
        }
        transport->count++;
    }
    if (was_empty) transport->last_progress_ms = now_ms;
    transport->state.sequence++;
    wm_psvr2_pointer_encode(transport->pending[index].bytes, &transport->state);
    transport->pending[index].transition = transition;
    transport->pending[index].queued_ms = now_ms;
    transport->oldest_pending_ms = transport->pending[transport->begin].queued_ms;
}

void wm_pointer_transport_flush(WmPointerTransport *transport, uint64_t now_ms) {
    if (transport->fd < 0) return;
    if (transport->count && age_ms(now_ms, transport->last_progress_ms) > 100) {
        disconnect_transport(transport, now_ms, WM_POINTER_DISCONNECT_WRITE_STALLED, 0);
        return;
    }
    if (transport->count && age_ms(now_ms, transport->oldest_pending_ms) > 100) {
        disconnect_transport(transport, now_ms, WM_POINTER_DISCONNECT_QUEUE_AGE, 0);
        return;
    }
    while (transport->count) {
        WmPointerPacket *packet = &transport->pending[transport->begin];
        ssize_t count = transport->write_bytes ?
            transport->write_bytes(transport->write_context, transport->fd,
                packet->bytes + transport->offset,
                WM_PSVR2_POINTER_PACKET_SIZE - transport->offset) :
            write(transport->fd, packet->bytes + transport->offset,
                WM_PSVR2_POINTER_PACKET_SIZE - transport->offset);
        if (count < 0 && errno == EINTR) continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            transport->would_block_count++;
            return;
        }
        if (count <= 0) {
            int error_code = count < 0 ? errno : 0;
            disconnect_transport(transport, now_ms, count < 0 ?
                WM_POINTER_DISCONNECT_WRITE_ERROR : WM_POINTER_DISCONNECT_WRITE_ZERO,
                error_code);
            return;
        }
        transport->offset += (unsigned)count;
        transport->bytes_written += (uint64_t)count;
        transport->last_progress_ms = now_ms;
        if (transport->offset == WM_PSVR2_POINTER_PACKET_SIZE) {
            transport->offset = 0;
            transport->begin = (transport->begin + 1) % WM_POINTER_PENDING_PACKETS;
            transport->count--;
            transport->packets_written++;
            transport->oldest_pending_ms = transport->count ?
                transport->pending[transport->begin].queued_ms : 0;
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
