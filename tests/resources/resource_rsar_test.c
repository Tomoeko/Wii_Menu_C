#include "wii_menu/resources/resource_rsar.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #condition);            \
            abort();                                                                   \
        }                                                                              \
    } while (0)

enum {
    ARCHIVE_SIZE = 1024,
    HEADER_OFFSET = 640,
    WAVE_OFFSET = 700,
    WAVE_INFO_OFFSET = 800
};

static void write_be32(uint8_t *bytes, uint32_t value) {
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

/* Open a bounded RSAR with valid outer directories and INFO references. The
 * malformed relative fields are inside a file payload, beyond open's scope. */
static WmRsar make_archive(uint8_t bytes[ARCHIVE_SIZE]) {
    memset(bytes, 0, ARCHIVE_SIZE);
    memcpy(bytes, "RSAR\xfe\xff\x01\x01", 8);
    write_be32(bytes + 8, ARCHIVE_SIZE);
    write_be32(bytes + 16, 64);
    write_be32(bytes + 20, 32);
    write_be32(bytes + 24, 248);
    write_be32(bytes + 28, ARCHIVE_SIZE - 248);
    memcpy(bytes + 64, "SYMB", 4);
    write_be32(bytes + 72, 8);
    memcpy(bytes + 248, "INFO", 4);
    write_be32(bytes + 260, 64);  /* Sound table at 320. */
    write_be32(bytes + 268, 80);  /* Bank table at 336. */
    write_be32(bytes + 284, 96);  /* File table at 352. */
    write_be32(bytes + 292, 112); /* Group table at 368. */
    write_be32(bytes + 336, 1);
    write_be32(bytes + 344, 128); /* Bank 0 at 384. */
    write_be32(bytes + 352, 1);
    write_be32(bytes + 360, 144); /* File 0 at 400. */
    write_be32(bytes + 368, 1);
    write_be32(bytes + 376, 224); /* Group 0 at 480. */
    write_be32(bytes + 388, 0);   /* Bank 0 uses file 0. */
    write_be32(bytes + 424, 192); /* File membership table at 448. */
    write_be32(bytes + 448, 1);
    write_be32(bytes + 456, 208); /* Membership entry at 464. */
    write_be32(bytes + 464, 0);   /* Group 0, item 0. */
    write_be32(bytes + 468, 0);
    write_be32(bytes + 496, HEADER_OFFSET);
    write_be32(bytes + 504, WAVE_OFFSET);
    write_be32(bytes + 516, 288); /* Group item table at 544. */
    write_be32(bytes + 544, 1);
    write_be32(bytes + 552, 304); /* Group item at 560. */

    WmRsar archive;
    char error[128] = {0};
    CHECK(wm_rsar_open(bytes, ARCHIVE_SIZE, &archive, error, sizeof(error)));
    return archive;
}

static void make_one_sample_wave(uint8_t bytes[ARCHIVE_SIZE], size_t info) {
    bytes[info] = 0;       /* Signed 8-bit PCM. */
    bytes[info + 2] = 1;   /* Mono. */
    bytes[info + 4] = 125; /* 32000 Hz. */
    write_be32(bytes + info + 8, 0);
    write_be32(bytes + info + 12, 0);
    write_be32(bytes + info + 16, 24);
    write_be32(bytes + info + 20, 40);
    write_be32(bytes + info + 24, 32);
    write_be32(bytes + info + 32, 200);
    bytes[WAVE_OFFSET + 40 + 200] = 42;
}

static void rejects_wrapped_direct_wave_offset(void) {
    uint8_t bytes[ARCHIVE_SIZE];
    WmRsar archive = make_archive(bytes);
    write_be32(bytes + HEADER_OFFSET, 1);
    write_be32(bytes + HEADER_OFFSET + 8, 160);
    write_be32(bytes + HEADER_OFFSET + 16, UINT32_MAX - 7);
    write_be32(bytes + HEADER_OFFSET + 24, 128);
    write_be32(bytes + 820, 192);
    write_be32(bytes + 832, 1);
    write_be32(bytes + 840, 224);
    write_be32(bytes + 864, 0);
    write_be32(bytes + 780, 128);
    write_be32(bytes + 960, 0);
    make_one_sample_wave(bytes, 896);
    WmRsarSound sound = {.file_index = 0, .type = 3, .extra = 960};
    WmAudioPcm output = {0};
    char error[128] = {0};
    CHECK(!wm_rsar_decode_direct_wave(&archive, &sound, &output, error, sizeof(error)));
    CHECK(output.samples == NULL);
}

static void rejects_wrapped_bank_wave_offset(void) {
    uint8_t bytes[ARCHIVE_SIZE];
    WmRsar archive = make_archive(bytes);
    write_be32(bytes + HEADER_OFFSET + 8, 160);
    write_be32(bytes + HEADER_OFFSET + 24, UINT32_MAX - 7);
    make_one_sample_wave(bytes, WAVE_INFO_OFFSET);
    WmAudioPcm output = {0};
    char error[128] = {0};
    CHECK(!wm_rsar_decode_bank_wave(&archive, 0, 0, &output, error, sizeof(error)));
    CHECK(output.samples == NULL);
}

static void rejects_wrapped_instrument_offset(void) {
    uint8_t bytes[ARCHIVE_SIZE];
    WmRsar archive = make_archive(bytes);
    bytes[HEADER_OFFSET + 5] = 1;
    write_be32(bytes + HEADER_OFFSET + 8, 160);
    write_be32(bytes + HEADER_OFFSET + 16, UINT32_MAX - 7);
    write_be32(bytes + WAVE_INFO_OFFSET, 7);
    write_be32(bytes + WAVE_INFO_OFFSET + 16, 0x3f800000);
    WmRsarInstrument instrument = {0};
    char error[128] = {0};
    CHECK(!wm_rsar_get_instrument(&archive, 0, 0, 60, 127, &instrument, error,
                                  sizeof(error)));
}

int main(void) {
    rejects_wrapped_direct_wave_offset();
    rejects_wrapped_bank_wave_offset();
    rejects_wrapped_instrument_offset();
    puts("Malformed RSAR relative offsets were rejected.");
    return 0;
}
