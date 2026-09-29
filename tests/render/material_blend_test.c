#include "material_blend.h"

#include <stdio.h>

#define CHECK(condition)                                                               \
    do {                                                                               \
        if (!(condition)) {                                                            \
            fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition);     \
            return 1;                                                                  \
        }                                                                              \
    } while (0)

int main(void) {
    WmMaterialQuad quad = {0};
    WmMaterialBlend blend = {0};

    CHECK(wm_material_blend_resolve(&quad, &blend));
    CHECK(blend.enabled && blend.source == 4 && blend.destination == 5);

    quad.has_blend_mode = true;
    quad.blend_mode[0] = 0;
    quad.blend_mode[1] = 255;
    quad.blend_mode[2] = 255;
    CHECK(wm_material_blend_resolve(&quad, &blend));
    CHECK(!blend.enabled);

    quad.blend_mode[0] = 1;
    for (unsigned source = 0; source < 8; source++) {
        for (unsigned destination = 0; destination < 8; destination++) {
            quad.blend_mode[1] = (uint8_t)source;
            quad.blend_mode[2] = (uint8_t)destination;
            CHECK(wm_material_blend_resolve(&quad, &blend));
            CHECK(blend.enabled && blend.source == source &&
                  blend.destination == destination);
        }
    }

    quad.blend_mode[1] = 8;
    quad.blend_mode[2] = 5;
    CHECK(!wm_material_blend_resolve(&quad, &blend));
    quad.blend_mode[1] = 4;
    quad.blend_mode[2] = 8;
    CHECK(!wm_material_blend_resolve(&quad, &blend));
    CHECK(!wm_material_blend_resolve(NULL, &blend));
    CHECK(!wm_material_blend_resolve(&quad, NULL));
    return 0;
}
