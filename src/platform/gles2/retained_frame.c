#include "retained_frame.h"

#include <stdlib.h>
#include <string.h>

void wm_gles2_retained_initialize(WmGles2RetainedFrame *frame) {
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    const char *setting = getenv("WM_GLES2_RETAIN_FRAME");
    frame->allowed =
        renderer && (strstr(renderer, "llvmpipe") || strstr(renderer, "softpipe"));
    if (setting)
        frame->allowed = strcmp(setting, "0") != 0;
    if (frame->allowed) {
        frame->commands = wm_frame_damage_create();
        if (!frame->commands)
            frame->allowed = false;
    }
}

void wm_gles2_retained_destroy(WmGles2RetainedFrame *frame) {
    wm_frame_damage_destroy(frame->commands);
    memset(frame, 0, sizeof(*frame));
}

bool wm_gles2_retained_begin(WmGles2RetainedFrame *frame, int width, int height,
                             WmColor clear) {
    frame->active = false;
    frame->recording = false;
    if (!frame->allowed || width <= 0 || height <= 0)
        return false;
    frame->width = width;
    frame->height = height;
    frame->clear = clear;
    frame->clip = (WmClipRect){0, 0, WM_FRAME_WIDTH, WM_FRAME_HEIGHT};
    frame->active = true;
    frame->recording = true;
    frame->region_active = false;
    wm_frame_damage_begin(frame->commands, width, height, clear);
    return true;
}

static void replay_region(WmGles2RetainedFrame *frame, WmPlatform *platform,
                          WmGles2Flush flush, WmViewport region) {
    frame->region = region;
    frame->region_active = true;
    wm_platform_set_clip(platform, NULL);
    glClearColor(frame->clear.r, frame->clear.g, frame->clear.b, frame->clear.a);
    glClear(GL_COLOR_BUFFER_BIT);
    size_t count = 0;
    const WmFrameCommand *commands = wm_frame_damage_commands(frame->commands, &count);
    for (size_t index = 0; index < count; index++) {
        const WmFrameCommand *command = &commands[index];
        if (!wm_frame_command_intersects(command, region, frame->width, frame->height))
            continue;
        wm_platform_set_clip(platform, &command->clip);
        if (command->kind == WM_FRAME_COMMAND_MATERIAL) {
            wm_platform_draw_material_quad(platform, &command->draw.material);
        } else {
            wm_platform_draw_vertices(platform, command->draw.vertices,
                                      command->texture);
        }
    }
    flush(platform);
}

void wm_gles2_retained_render(WmGles2RetainedFrame *frame, WmPlatform *platform,
                              WmGles2Flush flush) {
    if (!frame->recording)
        return;
    frame->recording = false;
    size_t count = 0;
    const WmViewport *regions = wm_frame_damage_regions(frame->commands, &count);
    for (size_t index = 0; index < count; index++)
        replay_region(frame, platform, flush, regions[index]);
    frame->region_active = false;
    wm_platform_set_clip(platform, NULL);
    wm_frame_damage_commit(frame->commands);
}

void wm_gles2_retained_materialize(WmGles2RetainedFrame *frame, WmPlatform *platform,
                                   WmGles2Flush flush) {
    if (!frame->recording)
        return;
    frame->recording = false;
    replay_region(frame, platform, flush,
                  (WmViewport){0, 0, frame->width, frame->height});
    frame->region_active = false;
    wm_platform_set_clip(platform, &frame->clip);
    wm_frame_damage_invalidate(frame->commands);
}
