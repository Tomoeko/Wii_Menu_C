#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *response;
static size_t response_size;
static size_t response_offset;
static size_t read_chunk;
static int open_failure;
static int write_failure;
static int read_failure;
static int close_failure;
static bool write_interrupted;
static bool read_interrupted;
static bool partial_write;
static unsigned open_count;
static unsigned write_count;
static unsigned close_count;

static int test_open(const char *path, int flags)
{
    assert(strcmp(path, "/proc/stage3") == 0);
    assert(flags == (O_RDWR | O_CLOEXEC));
    ++open_count;
    if (open_failure) {
        errno = open_failure;
        return -1;
    }
    return 7;
}

static ssize_t test_write(int descriptor, const void *data, size_t size)
{
    assert(descriptor == 7);
    assert(size == 10 && memcmp(data, "audiopatch", size) == 0);
    ++write_count;
    if (write_interrupted) {
        write_interrupted = false;
        errno = EINTR;
        return -1;
    }
    if (write_failure) {
        errno = write_failure;
        return -1;
    }
    return (ssize_t)(partial_write ? size - 1 : size);
}

static ssize_t test_read(int descriptor, void *data, size_t size)
{
    assert(descriptor == 7 && size);
    if (read_interrupted) {
        read_interrupted = false;
        errno = EINTR;
        return -1;
    }
    if (read_failure) {
        errno = read_failure;
        return -1;
    }
    size_t count = response_size - response_offset;
    if (count > size) count = size;
    if (count > read_chunk) count = read_chunk;
    memcpy(data, response + response_offset, count);
    response_offset += count;
    return (ssize_t)count;
}

static int test_close(int descriptor)
{
    assert(descriptor == 7);
    ++close_count;
    if (close_failure) {
        errno = close_failure;
        return -1;
    }
    return 0;
}

#define open test_open
#define write test_write
#define read test_read
#define close test_close
#include "../../src/platform/psvr2/audio_stage3.h"
#undef open
#undef write
#undef read
#undef close

static void reset(const char *reply)
{
    response = reply;
    response_size = strlen(reply);
    response_offset = 0;
    read_chunk = 7;
    open_failure = write_failure = read_failure = close_failure = 0;
    write_interrupted = read_interrupted = partial_write = false;
    open_count = write_count = close_count = 0;
    errno = 0;
}

static void expect_failure(int expected)
{
    assert(!wm_audio_stage3_patch());
    assert(errno == expected);
    assert(open_count == 1);
    assert(close_count == (open_failure ? 0 : 1));
}

int main(void)
{
    static const char okay[] = "OK audiopatch prep=1 port=1 ts=1";
    reset(okay);
    write_interrupted = read_interrupted = true;
    assert(wm_audio_stage3_patch());
    assert(open_count == 1 && write_count == 2 && close_count == 1);
    assert(response_offset == response_size);

    static const char *invalid[] = {
        "ERR audiopatch: symbols missing",
        "OK input bridge ttyGS1 software ring",
        "OK audiopatch prep=1 port=1 ts=",
        "OK audiopatch prep=256 port=1 ts=1",
        "OK audiopatch prep=1 port=4294967296 ts=1",
        "OK audiopatch prep=1 port=1 ts=256",
        "OK audiopatch prep=-1 port=1 ts=1",
        "OK audiopatch prep=1 port=1 ts=1 extra",
        ""
    };
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        reset(invalid[index]);
        expect_failure(EPROTO);
    }
    reset("OK audiopatch prep=255 port=4294967295 ts=255\n");
    assert(wm_audio_stage3_patch());

    reset(okay);
    open_failure = ENOENT;
    expect_failure(ENOENT);
    assert(write_count == 0);
    reset(okay);
    write_failure = EPIPE;
    close_failure = EBADF;
    expect_failure(EPIPE);
    assert(response_offset == 0);
    reset(okay);
    partial_write = true;
    expect_failure(EIO);
    assert(write_count == 1 && response_offset == 0);
    reset(okay);
    read_failure = EACCES;
    expect_failure(EACCES);
    reset(okay);
    close_failure = EIO;
    expect_failure(EIO);

    char oversized[257];
    memset(oversized, 'x', sizeof(oversized) - 1);
    oversized[sizeof(oversized) - 1] = '\0';
    reset(oversized);
    expect_failure(EOVERFLOW);

    char embedded[sizeof(okay) + 1];
    memcpy(embedded, okay, sizeof(okay));
    embedded[sizeof(okay)] = 'x';
    reset(embedded);
    response_size = sizeof(embedded);
    expect_failure(EPROTO);
    puts("PSVR2 audio Stage3 status tests passed.");
    return 0;
}
