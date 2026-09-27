#ifndef WII_MENU_PSVR2_VR_LAYOUT_H
#define WII_MENU_PSVR2_VR_LAYOUT_H

#include <stdbool.h>

/* Native output is two 2000 x 2040 eyes. The menu raster is anamorphic;
 * its displayed aspect is a separate setting. Coordinates are top-left. */
typedef struct WmVrConfig {
    float horizontal_fov_degrees;
    float distance_m;
    float ipd_mm;
    float aspect_ratio;
    float convergence_pixels;
    float vertical_offset_pixels;
    float luminance;
} WmVrConfig;

typedef struct WmVrCalibration {
    float left_x;
    float left_y;
    float right_x;
    float right_y;
    bool loaded;
} WmVrCalibration;

typedef struct WmVrEyeRect {
    float x;
    float y;
    float width;
    float height;
} WmVrEyeRect;

WmVrConfig wm_vr_default_config(void);
bool wm_vr_validate_config(const WmVrConfig *config);
/* Missing files leave defaults intact; malformed files are rejected as a unit. */
bool wm_vr_load_config(const char *path, WmVrConfig *config);
bool wm_vr_load_calibration(const char *path, WmVrCalibration *calibration);
bool wm_vr_eye_rects(const WmVrConfig *config,
                     const WmVrCalibration *calibration,
                     WmVrEyeRect eyes[2]);

#endif
