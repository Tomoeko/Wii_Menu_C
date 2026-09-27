#define _POSIX_C_SOURCE 200809L
#include "vr_layout.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

WmVrConfig wm_vr_default_config(void)
{
    return (WmVrConfig){66.0f, 2.0f, 64.0f, 16.0f / 9.0f, 0.0f, 0.0f, 0.65f};
}

bool wm_vr_validate_config(const WmVrConfig *config)
{
    return config && isfinite(config->horizontal_fov_degrees) &&
        config->horizontal_fov_degrees >= 40.0f &&
        config->horizontal_fov_degrees <= 70.0f &&
        isfinite(config->distance_m) && config->distance_m >= 1.0f &&
        config->distance_m <= 5.0f &&
        isfinite(config->ipd_mm) && config->ipd_mm >= 50.0f &&
        config->ipd_mm <= 75.0f &&
        isfinite(config->aspect_ratio) && config->aspect_ratio >= 4.0f / 3.0f &&
        config->aspect_ratio <= 16.0f / 9.0f &&
        isfinite(config->convergence_pixels) &&
        fabsf(config->convergence_pixels) <= 32.0f &&
        isfinite(config->vertical_offset_pixels) &&
        fabsf(config->vertical_offset_pixels) <= 150.0f &&
        isfinite(config->luminance) && config->luminance >= 0.1f &&
        config->luminance <= 0.8f;
}

bool wm_vr_load_config(const char *path, WmVrConfig *config)
{
    if (!path || !config) return false;
    FILE *file = fopen(path, "r");
    if (!file) return false;
    WmVrConfig candidate = *config;
    char line[256];
    bool valid = true;
    unsigned fields = 0;
    while (fgets(line, sizeof(line), file)) {
        char key[64], extra;
        float value;
        char *first = line;
        while (*first == ' ' || *first == '\t') ++first;
        if (*first == '#' || *first == '\n' || !*first) continue;
        if (!strchr(line, '\n') && !feof(file)) { valid = false; break; }
        if (sscanf(first, " %63[^= \t] = %f %c", key, &value, &extra) != 2) {
            valid = false;
            break;
        }
        unsigned bit = 0;
        float *destination = NULL;
        if (!strcmp(key, "horizontal_fov_degrees")) {
            bit = 1; destination = &candidate.horizontal_fov_degrees;
        } else if (!strcmp(key, "distance_m")) {
            bit = 2; destination = &candidate.distance_m;
        } else if (!strcmp(key, "ipd_mm")) {
            bit = 4; destination = &candidate.ipd_mm;
        } else if (!strcmp(key, "aspect_ratio")) {
            bit = 8; destination = &candidate.aspect_ratio;
        } else if (!strcmp(key, "convergence_pixels")) {
            bit = 16; destination = &candidate.convergence_pixels;
        } else if (!strcmp(key, "vertical_offset_pixels")) {
            bit = 32; destination = &candidate.vertical_offset_pixels;
        } else if (!strcmp(key, "luminance")) {
            bit = 64; destination = &candidate.luminance;
        }
        if (!destination || (fields & bit)) { valid = false; break; }
        fields |= bit;
        *destination = value;
    }
    if (ferror(file)) valid = false;
    fclose(file);
    if (!valid || !wm_vr_validate_config(&candidate)) return false;
    *config = candidate;
    return true;
}

static float little_endian_float(const uint8_t *bytes)
{
    uint32_t bits = (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
        (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}

bool wm_vr_load_calibration(const char *path, WmVrCalibration *calibration)
{
    if (!path || !calibration) return false;
    *calibration = (WmVrCalibration){0};
    FILE *file = fopen(path, "rb");
    if (!file) return false;
    /* The 06.00 open_vrhmd format has an opaque 32-byte prefix followed by
     * 18 little-endian float fields. Only the documented panel X/Y deltas
     * are consumed. Pose and distortion are not inferred from these fields. */
    uint8_t bytes[72];
    bool read_ok = fseek(file, 32, SEEK_SET) == 0 &&
        fread(bytes, 1, sizeof(bytes), file) == sizeof(bytes);
    fclose(file);
    if (!read_ok) return false;
    float offsets[4];
    for (unsigned index = 0; index < 4; ++index) {
        offsets[index] = little_endian_float(bytes + index * 4);
        if (!isfinite(offsets[index]) || fabsf(offsets[index]) > 32.0f)
            return false;
    }
    *calibration = (WmVrCalibration){offsets[0], offsets[1],
        offsets[2], offsets[3], true};
    return true;
}

bool wm_vr_eye_rects(const WmVrConfig *config,
                     const WmVrCalibration *calibration,
                     WmVrEyeRect eyes[2])
{
    if (!eyes || !wm_vr_validate_config(config)) return false;
    WmVrCalibration zero = {0};
    if (!calibration) calibration = &zero;
    float offsets[] = {calibration->left_x, calibration->left_y,
        calibration->right_x, calibration->right_y};
    for (unsigned index = 0; index < 4; ++index)
        if (!isfinite(offsets[index]) || fabsf(offsets[index]) > 32.0f)
            return false;
    /* 1020px focal scale and asymmetric nominal optical centers follow
     * open_vrhmd's current flat-plane projection; they are not lens fitting. */
    float width = 2040.0f * tanf(config->horizontal_fov_degrees *
                                 0.008726646259971648f);
    float height = width / config->aspect_ratio;
    float disparity = 1020.0f * (config->ipd_mm * 0.0005f) /
        config->distance_m + config->convergence_pixels;
    eyes[0] = (WmVrEyeRect){1108.0f + offsets[0] + disparity - width / 2,
        1019.0f + offsets[1] + config->vertical_offset_pixels - height / 2,
        width, height};
    eyes[1] = (WmVrEyeRect){2000.0f + 892.0f + offsets[2] - disparity - width / 2,
        1019.0f + offsets[3] + config->vertical_offset_pixels - height / 2,
        width, height};
    for (unsigned eye = 0; eye < 2; ++eye) {
        if (eyes[eye].x < eye * 2000.0f || eyes[eye].y < 0.0f ||
            eyes[eye].x + width > (eye + 1) * 2000.0f ||
            eyes[eye].y + height > 2040.0f) return false;
    }
    return true;
}
