#define _POSIX_C_SOURCE 200809L
#include "vr_layout.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void test_projection(void)
{
    WmVrConfig config = wm_vr_default_config();
    WmVrEyeRect eyes[2];
    assert(wm_vr_validate_config(&config));
    assert(wm_vr_eye_rects(&config, NULL, eyes));
    assert(fabsf(eyes[0].width / eyes[0].height - 16.0f / 9.0f) < 0.0001f);
    assert(fabsf(eyes[0].width - eyes[1].width) < 0.001f);
    assert(eyes[0].x > 200.0f && eyes[1].x > 2000.0f);
    assert(eyes[0].y > 500.0f && eyes[0].y + eyes[0].height < 1540.0f);
    float near_center = eyes[0].x + eyes[0].width / 2.0f;
    config.distance_m = 5.0f;
    assert(wm_vr_eye_rects(&config, NULL, eyes));
    assert(eyes[0].x + eyes[0].width / 2.0f < near_center);
    config.aspect_ratio = 4.0f / 3.0f;
    assert(wm_vr_eye_rects(&config, NULL, eyes));
    assert(fabsf(eyes[0].width / eyes[0].height - 4.0f / 3.0f) < 0.0001f);

    /* All supported extremes stay within each eye with a black surround. */
    for (unsigned combination = 0; combination < 256; ++combination) {
        config.horizontal_fov_degrees = combination & 1 ? 70.0f : 40.0f;
        config.distance_m = combination & 2 ? 5.0f : 1.0f;
        config.ipd_mm = combination & 4 ? 75.0f : 50.0f;
        config.aspect_ratio = combination & 8 ? 16.0f / 9.0f : 4.0f / 3.0f;
        config.convergence_pixels = combination & 16 ? 32.0f : -32.0f;
        config.vertical_offset_pixels = combination & 32 ? 150.0f : -150.0f;
        float dx = combination & 64 ? 32.0f : -32.0f;
        float dy = combination & 128 ? 32.0f : -32.0f;
        WmVrCalibration calibration = {dx, dy, -dx, -dy, true};
        assert(wm_vr_eye_rects(&config, &calibration, eyes));
        for (unsigned eye = 0; eye < 2; ++eye) {
            assert(eyes[eye].x > eye * 2000.0f);
            assert(eyes[eye].x + eyes[eye].width < (eye + 1) * 2000.0f);
            assert(eyes[eye].y > 0 && eyes[eye].y + eyes[eye].height < 2040);
        }
    }
    config.distance_m = NAN;
    assert(!wm_vr_eye_rects(&config, NULL, eyes));
    config = wm_vr_default_config();
    config.luminance = 1.0f;
    assert(!wm_vr_validate_config(&config));
    WmVrCalibration malformed = {INFINITY, 0, 0, 0, true};
    config = wm_vr_default_config();
    assert(!wm_vr_eye_rects(&config, &malformed, eyes));
}

static void write_config(const char *path, const char *text)
{
    FILE *file = fopen(path, "w");
    assert(file);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
}

static void test_config(const char *path)
{
    WmVrConfig config = wm_vr_default_config();
    write_config(path, "# Personal projection\nhorizontal_fov_degrees = 60\n"
                       "distance_m = 3\nipd_mm=68\naspect_ratio=1.3333334\n"
                       "convergence_pixels=-2\nvertical_offset_pixels=12\n"
                       "luminance=0.5\n");
    assert(wm_vr_load_config(path, &config));
    assert(config.distance_m == 3.0f && config.luminance == 0.5f);
    WmVrConfig prior = config;
    const char *invalid[] = {
        "distance_m=0\n", "distance_m=nan\n", "unknown=1\n",
        "distance_m=2\ndistance_m=3\n", "ipd_mm=64 garbage\n",
        "convergence_pixels=33\n", "luminance=0.9\n"
    };
    for (unsigned index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        write_config(path, invalid[index]);
        assert(!wm_vr_load_config(path, &config));
        assert(memcmp(&prior, &config, sizeof(config)) == 0);
    }
}

static void store_float(uint8_t *bytes, float value)
{
    uint32_t word;
    memcpy(&word, &value, 4);
    for (unsigned index = 0; index < 4; ++index) bytes[index] = word >> (index * 8);
}

static void test_calibration(const char *path)
{
    uint8_t bytes[104] = {0};
    store_float(bytes + 32, 2.5f);
    store_float(bytes + 36, -1.5f);
    store_float(bytes + 40, -0.25f);
    store_float(bytes + 44, 0.125f);
    FILE *file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fclose(file) == 0);
    WmVrCalibration calibration;
    assert(wm_vr_load_calibration(path, &calibration));
    assert(calibration.loaded && calibration.left_x == 2.5f &&
           calibration.right_y == 0.125f);
    store_float(bytes + 32, INFINITY);
    file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, sizeof(bytes), file) == sizeof(bytes));
    assert(fclose(file) == 0);
    assert(!wm_vr_load_calibration(path, &calibration) && !calibration.loaded);
    file = fopen(path, "wb");
    assert(file && fwrite(bytes, 1, 40, file) == 40);
    assert(fclose(file) == 0);
    assert(!wm_vr_load_calibration(path, &calibration));
}

int main(void)
{
    char path[] = "/tmp/wm-psvr2-layout-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    close(descriptor);
    test_projection();
    test_config(path);
    test_calibration(path);
    assert(unlink(path) == 0);
    puts("PSVR2 projection, configuration, and calibration tests passed.");
    return 0;
}
