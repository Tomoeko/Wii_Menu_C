#ifndef WII_MENU_PSVR2_AUDIO_STAGE3_H
#define WII_MENU_PSVR2_AUDIO_STAGE3_H

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static inline bool wm_audio_stage3_number(const char **cursor, uint32_t maximum)
{
    const char *text = *cursor;
    if (*text < '0' || *text > '9') return false;
    uint32_t value = 0;
    do {
        uint32_t digit = (uint32_t)(*text - '0');
        if (value > (maximum - digit) / 10) return false;
        value = value * 10 + digit;
        ++text;
    } while (*text >= '0' && *text <= '9');
    *cursor = text;
    return true;
}

static inline bool wm_audio_stage3_status(const char *reply)
{
    static const char prefix[] = "OK audiopatch prep=";
    if (strncmp(reply, prefix, sizeof(prefix) - 1) != 0) return false;
    const char *cursor = reply + sizeof(prefix) - 1;
    if (!wm_audio_stage3_number(&cursor, UINT8_MAX) ||
        strncmp(cursor, " port=", 6) != 0) return false;
    cursor += 6;
    if (!wm_audio_stage3_number(&cursor, UINT32_MAX) ||
        strncmp(cursor, " ts=", 4) != 0) return false;
    cursor += 4;
    if (!wm_audio_stage3_number(&cursor, UINT8_MAX)) return false;
    if (*cursor == '\n') ++cursor;
    return *cursor == '\0';
}

/* Stage3 returns the write length even when the command fails. Read the
 * synchronous result through the same open file before trusting the patch.
 * Its proc writer does not advance the read offset. */
static inline bool wm_audio_stage3_patch(void)
{
    int descriptor = open("/proc/stage3", O_RDWR | O_CLOEXEC);
    if (descriptor < 0) return false;
    static const char command[] = "audiopatch";
    ssize_t count;
    do {
        count = write(descriptor, command, sizeof(command) - 1);
    } while (count < 0 && errno == EINTR);
    int failure = count < 0 ? errno : 0;
    if (!failure && count != (ssize_t)(sizeof(command) - 1)) failure = EIO;

    char reply[257];
    size_t length = 0;
    while (!failure) {
        do {
            count = read(descriptor, reply + length, sizeof(reply) - 1 - length);
        } while (count < 0 && errno == EINTR);
        if (count < 0) {
            failure = errno;
        } else if (!count) {
            reply[length] = '\0';
            if (strlen(reply) != length || !wm_audio_stage3_status(reply)) failure = EPROTO;
            break;
        } else {
            length += (size_t)count;
            if (length == sizeof(reply) - 1) failure = EOVERFLOW;
        }
    }
    if (close(descriptor) != 0 && !failure) failure = errno;
    if (failure) errno = failure;
    return failure == 0;
}

#endif
