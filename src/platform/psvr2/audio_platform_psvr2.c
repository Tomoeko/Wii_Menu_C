#define _DEFAULT_SOURCE
#include "audio_platform.h"
#include "open_vrhmd.h"
#include "audio_stage3.h"
#include "audio_codec_path.h"

#include <linux/i2c.h>
#include <linux/i2c-dev.h>
#include <inttypes.h>
#include <math.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <time.h>

/* Retail 06.00 DMA and codec sequence follows first-party open_vrhmd/audio.
 * This streams the existing mixer directly, with no MP3 decoder dependency.
 * Device SRAM requires volatile aligned 32-bit stores; never memcpy it. */
#define AFE_REG_BASE 0x10c00000ULL
#define AFE_REG_SIZE 0x1000
#define AFE_SRAM_BASE 0x10c01000ULL
#define AFE_SRAM_SIZE 0x2400
#define DL12_SRAM_SIZE 4608
#define DL12_HALF_FRAMES 288
#define AFE_DAC_CON0 0x0010
#define AFE_DL12_BASE_R 0x0340
#define AFE_DL12_CUR_R 0x0344
#define AFE_DL12_END_R 0x0348
#define AFE_CONN_MUX_CFG 0x0af8
#define WM1801_I2C_BUS 0
#define WM1801_I2C_ADDR 0x4A
#define WM1801_VOLUME_UPDATE 0x100
#define WM1801_DAC_VOLUME_LEFT 10
#define WM1801_DAC_VOLUME_RIGHT 11
#define WM1801_DAC_0DB 0xC0

struct WmAudioDevice {
    int audio_fd;
    int memory_fd;
    int i2c_fd;
    volatile uint32_t *registers;
    volatile uint32_t *sram;
    WmAudioRender render;
    void *context;
    uint16_t headphone_volume;
    pthread_t thread;
    atomic_bool running;
    bool thread_started;
};

static int wm1801_write(WmAudioDevice *device, uint8_t reg, uint16_t val)
{
    uint8_t buf[3] = {reg, (val >> 8) & 0xFF, val & 0xFF};
    if (write(device->i2c_fd, buf, 3) != 3) {
        ERR("WM1801: write reg %d = 0x%04x failed (errno=%d)", reg, val, errno);
        return -1;
    }
    return 0;
}

/* Read a 16-bit value from a WM1801 register. */
static int wm1801_read(WmAudioDevice *device, uint8_t reg, uint16_t *val)
{
    struct i2c_msg msgs[2];
    struct i2c_rdwr_ioctl_data rdwr;

    uint8_t reg_buf = reg;
    uint8_t data_buf[2] = {0};

    msgs[0].addr = WM1801_I2C_ADDR;
    msgs[0].flags = 0; /* write */
    msgs[0].len = 1;
    msgs[0].buf = &reg_buf;

    msgs[1].addr = WM1801_I2C_ADDR;
    msgs[1].flags = I2C_M_RD; /* read */
    msgs[1].len = 2;
    msgs[1].buf = data_buf;

    rdwr.msgs = msgs;
    rdwr.nmsgs = 2;

    if (ioctl(device->i2c_fd, I2C_RDWR, &rdwr) < 0) {
        ERR("WM1801: read reg %d failed (errno=%d)", reg, errno);
        return -1;
    }
    *val = (uint16_t)((uint16_t)data_buf[0] << 8 | data_buf[1]);
    return 0;
}

/* Read-modify-write a WM1801 register: reg = (reg & ~mask) | (mask & value) */
static int wm1801_rmw(WmAudioDevice *device, uint8_t reg, uint16_t mask, uint16_t value)
{
    uint16_t cur;
    if (wm1801_read(device, reg, &cur) < 0)
        return -1;
    uint16_t newval = (cur & ~mask) | (mask & value);
    return wm1801_write(device, reg, newval);
}

static int wm1801_update(void *context, uint8_t reg, uint16_t mask, uint16_t value)
{
    return wm1801_rmw(context, reg, mask, value);
}

/* Initialize the WM1801 codec — replicated from vrhmd_main.elf sub_13B4C.
 * This powers on the DAC, headphone amp, sets I2S format, and unmutes. */
static int wm1801_init(WmAudioDevice *device, int volume_pct)
{
    uint16_t vol_val;
    if (wm_audio_codec_headphone_code(volume_pct, &vol_val) < 0)
        return -1;
    char i2c_path[32];
    snprintf(i2c_path, sizeof(i2c_path), "/dev/i2c-%d", WM1801_I2C_BUS);

    device->i2c_fd = open(i2c_path, O_RDWR);
    if (device->i2c_fd < 0) {
        ERR("Cannot open %s (errno=%d)", i2c_path, errno);
        return -1;
    }

    if (ioctl(device->i2c_fd, I2C_SLAVE, WM1801_I2C_ADDR) < 0) {
        ERR("Cannot set I2C slave addr 0x%02x (errno=%d)", WM1801_I2C_ADDR, errno);
        close(device->i2c_fd);
        device->i2c_fd = -1;
        return -1;
    }

    /* Verify WM1801 presence — read Device ID register (reg 1) */
    uint16_t dev_id = 0;
    if (wm1801_read(device, 1, &dev_id) < 0) {
        ERR("WM1801 not responding on I2C");
        close(device->i2c_fd);
        device->i2c_fd = -1;
        return -1;
    }
    LOG("WM1801 Device ID: 0x%04x (rev %d)", dev_id, (dev_id >> 9) & 7);
    fflush(stdout);

    /* Headphone power sequence from vrhmd_main.elf sub_13B4C, with
     * microphone monitoring omitted for menu playback. */

    /* Step 1: Software reset */
    if (wm1801_write(device, 15, 0) < 0)
        goto fail;
    LOG("WM1801: software reset sent");
    fflush(stdout);
    usleep(10000); /* 10ms */

    /* Step 2: Power management and anti-pop */
    if (wm1801_write(device, 25, 234) < 0) /* 0xEA — power mgmt 1 */
        goto fail;
    if (wm1801_write(device, 28, 24) < 0) /* 0x18 — anti-pop control */
        goto fail;
    LOG("WM1801: power mgmt registers written");
    fflush(stdout);
    usleep(50000); /* 50ms for power-up */

    /* Step 3: Configure basic output routing */
    if (wm1801_write(device, 82, 0) < 0) /* Reg 0x52 */
        goto fail;
    /* The menu has no microphone-monitoring control. Keep ambient microphone
     * audio out of both headphone DAC channels. */
    if (wm1801_write(device, WM_AUDIO_CODEC_SIDETONE_0, 0) < 0 ||
        wm1801_write(device, WM_AUDIO_CODEC_SIDETONE_1, 0) < 0)
        goto fail;
    if (wm1801_write(device, 71, 435) < 0) /* Reg 0x47 = 0x01B3 */
        goto fail;
    if (wm1801_write(device, 32, 16) < 0) /* Reg 0x20 = 0x10 (left ADC input path) */
        goto fail;
    if (wm1801_rmw(device, 48, 1, 0) < 0) /* Reg 0x30: clear bit 0 */
        goto fail;

    /* Set both gains before powering/unmuting the outputs, matching Sony's
     * initialization order. Retain the player's attenuated headphone ceiling. */
    device->headphone_volume = vol_val;
    if (wm1801_write(device, 2, vol_val | 0x80) < 0 ||
        wm1801_write(device, 3, vol_val | 0x80 | WM1801_VOLUME_UPDATE) < 0)
        goto fail;
    if (wm1801_write(device, WM1801_DAC_VOLUME_LEFT, WM1801_DAC_0DB) < 0 ||
        wm1801_write(device, WM1801_DAC_VOLUME_RIGHT, WM1801_DAC_0DB | WM1801_VOLUME_UPDATE) < 0)
        goto fail;

    /* Step 4: Start the stock headphone power sequencer */
    if (wm1801_write(device, 87, 48) < 0) /* Reg 0x57 = 0x30 */
        goto fail;
    if (wm1801_write(device, 88, 256) < 0) /* Reg 0x58 = 0x100 */
        goto fail;
    if (wm1801_write(device, 90, 128) < 0) /* Reg 0x5A = 0x80 */
        goto fail;

    /* Step 5: Wait for VMID charge (CRITICAL) */
    LOG("WM1801: waiting 400ms for VMID charge...");
    fflush(stdout);
    usleep(400000);

    /* Step 6: Reassert playback-only routing and unmute the DAC. Register 0
     * belongs to the microphone PGA and is not a headphone mute control. */
    if (wm_audio_codec_start_playback(device, wm1801_update, vol_val) < 0)
        goto fail;

    LOG("WM1801: init complete, volume=%d%% (val=0x%02x)", volume_pct, vol_val);
    fflush(stdout);

    return 0;

fail:
    {
        int saved_errno = errno;
        wm_audio_codec_mute_playback(device, wm1801_update);
        close(device->i2c_fd);
        device->i2c_fd = -1;
        errno = saved_errno;
    }
    return -1;
}

/* Called by the shared display teardown before releasing its device nodes. */
int audio_stop_hardware(void)
{
    int descriptor = open("/dev/audio", O_RDWR | O_CLOEXEC);
    if (descriptor >= 0) {
        ioctl(descriptor, SIE_AUDIO_IO_TRANSFER_STOP);
        close(descriptor);
    }
    return wm_audio_stage3_patch() ? 0 : -1;
}

static int64_t wm_audio_milliseconds(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0)
        return -1;
    return (int64_t)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void *wm_audio_output(void *context)
{
    WmAudioDevice *device = context;
    float mixed[DL12_HALF_FRAMES * 2];
    int last_half = -1;
    int64_t last_transition = wm_audio_milliseconds();
    int64_t statistics_since = last_transition;
    const char *statistics_option = getenv("WM_PSVR2_AUDIO_STATS");
    bool statistics = statistics_option && strcmp(statistics_option, "1") == 0;
    uint64_t fills = 0, nonzero = 0, invalid_cursors = 0;
    uint32_t peak = 0;
    while (atomic_load_explicit(&device->running, memory_order_relaxed) &&
           !application_stop_requested) {
        uint32_t cursor = device->registers[AFE_DL12_CUR_R / 4];
        int half = -1;
        if (cursor >= AFE_SRAM_BASE && cursor < AFE_SRAM_BASE + DL12_SRAM_SIZE)
            half = (cursor - AFE_SRAM_BASE) / (DL12_SRAM_SIZE / 2);
        if (statistics && half < 0) ++invalid_cursors;
        if (half >= 0 && half != last_half) {
            last_half = half;
            last_transition = wm_audio_milliseconds();
            device->render(device->context, mixed, DL12_HALF_FRAMES);
            if (statistics) ++fills;
            volatile uint32_t *output = device->sram + (1 - half) * DL12_HALF_FRAMES * 2;
            for (size_t sample = 0; sample < DL12_HALF_FRAMES * 2; ++sample) {
                float value = isfinite(mixed[sample]) ? mixed[sample] : 0.0f;
                value = fminf(1.0f, fmaxf(-1.0f, value));
                /* Fixed half amplitude plus codec attenuation. Volume remains
                 * controlled by the portable mixer; startup cannot blast. */
                int32_t converted = (int32_t)(value * 1073741823.0f);
                output[sample] = (uint32_t)converted;
                if (statistics) {
                    uint32_t magnitude = converted < 0 ?
                        (uint32_t)-(int64_t)converted : (uint32_t)converted;
                    nonzero += magnitude > 0;
                    if (magnitude > peak) peak = magnitude;
                }
            }
        }
        int64_t now = wm_audio_milliseconds();
        if (statistics && now - statistics_since >= 1000) {
            fprintf(stderr, "PSVR2 audio DMA: fills=%" PRIu64
                    " nonzero=%" PRIu64 " peak=%" PRIu32
                    " invalid_cursors=%" PRIu64 " cursor=%08" PRIx32 "\n",
                    fills, nonzero, peak, invalid_cursors, cursor);
            statistics_since = now;
            fills = nonzero = invalid_cursors = 0;
            peak = 0;
        }
        if (now < 0 || last_transition < 0 || now - last_transition > 1000) {
            fprintf(stderr, "PSVR2: audio DMA cursor stalled; output stopped.\n");
            atomic_store(&device->running, false);
            wm_audio_codec_mute_playback(device, wm1801_update);
            ioctl(device->audio_fd, SIE_AUDIO_IO_TRANSFER_STOP);
            break;
        }
        usleep(500);
    }
    return NULL;
}

WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context)
{
    if (!render)
        return NULL;
    WmAudioDevice *device = calloc(1, sizeof(*device));
    if (!device)
        return NULL;
    device->audio_fd = device->memory_fd = device->i2c_fd = -1;
    device->render = render;
    device->context = context;
    device->audio_fd = open("/dev/audio", O_RDWR | O_CLOEXEC);
    if (device->audio_fd < 0 || !wm_audio_stage3_patch())
        goto fail;
    usleep(10000);
    if (ioctl(device->audio_fd, SIE_AUDIO_IO_PREPARE) < 0 || wm1801_init(device, 50) < 0)
        goto fail;
    device->memory_fd = open("/dev/mem", O_RDWR | O_SYNC | O_CLOEXEC);
    if (device->memory_fd < 0)
        goto fail;
    device->registers = mmap(NULL, AFE_REG_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED,
                             device->memory_fd, AFE_REG_BASE);
    if (device->registers == MAP_FAILED) {
        device->registers = NULL;
        goto fail;
    }
    device->sram = mmap(NULL, AFE_SRAM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, device->memory_fd,
                        AFE_SRAM_BASE);
    if (device->sram == MAP_FAILED) {
        device->sram = NULL;
        goto fail;
    }
    for (size_t word = 0; word < DL12_SRAM_SIZE / 4; ++word)
        device->sram[word] = 0;
    struct audio_io_transfer_start_req transfer = {1, 48000};
    if (ioctl(device->audio_fd, SIE_AUDIO_IO_TRANSFER_START, &transfer) < 0 ||
        !wm_audio_stage3_patch())
        goto fail;
    usleep(10000);
    if (ioctl(device->audio_fd, SIE_AUDIO_IO_PREPARE) < 0)
        goto fail;
    device->registers[AFE_DL12_BASE_R / 4] = AFE_SRAM_BASE;
    device->registers[AFE_DL12_END_R / 4] = AFE_SRAM_BASE + DL12_SRAM_SIZE - 1;
    device->registers[AFE_DAC_CON0 / 4] |= 1U << 8;
    uint32_t mux = device->registers[AFE_CONN_MUX_CFG / 4];
    device->registers[AFE_CONN_MUX_CFG / 4] = (mux & ~0xFFFFU) | 0x3210;
    /* The stock headphone sequencer can overwrite routing and volume. Reapply
     * the bounded playback path after the complete driver startup sequence. */
    if (wm_audio_codec_start_playback(device, wm1801_update,
                                      device->headphone_volume) < 0)
        goto fail;
    atomic_store(&device->running, true);
    if (pthread_create(&device->thread, NULL, wm_audio_output, device) != 0)
        goto fail;
    device->thread_started = true;
    return device;

fail:
    fprintf(stderr, "PSVR2: native audio initialization failed: %s.\n", strerror(errno));
    wm_audio_device_close(device);
    return NULL;
}

void wm_audio_device_close(WmAudioDevice *device)
{
    if (!device)
        return;
    atomic_store(&device->running, false);
    if (device->thread_started)
        pthread_join(device->thread, NULL);
    /* Silence headphone playback before stopping DMA or clearing its ring. */
    if (device->i2c_fd >= 0)
        wm_audio_codec_mute_playback(device, wm1801_update);
    if (device->audio_fd >= 0) {
        ioctl(device->audio_fd, SIE_AUDIO_IO_TRANSFER_STOP);
        close(device->audio_fd);
    }
    if (device->sram) {
        for (size_t word = 0; word < DL12_SRAM_SIZE / 4; ++word)
            device->sram[word] = 0;
        munmap((void *)device->sram, AFE_SRAM_SIZE);
    }
    if (device->registers)
        munmap((void *)device->registers, AFE_REG_SIZE);
    if (device->memory_fd >= 0)
        close(device->memory_fd);
    if (device->i2c_fd >= 0) {
        close(device->i2c_fd);
    }
    free(device);
}
