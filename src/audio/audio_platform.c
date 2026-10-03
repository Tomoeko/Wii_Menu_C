#include "audio_platform.h"
#include "console_common/audio/output.h"

#include <stdlib.h>

struct WmAudioDevice {
    CcAudioOutput *output;
};

WmAudioDevice *wm_audio_device_open(WmAudioRender render, void *context) {
    if (!render)
        return NULL;
    WmAudioDevice *device = calloc(1, sizeof(*device));
    if (!device)
        return NULL;
    CcAudioOutputOptions options = {.sample_rate = 48000,
                                    .render = render,
                                    .context = context,
                                    .mode = CC_AUDIO_OUTPUT_DIRECT};
    device->output = cc_audio_output_open(&options);
    if (!device->output || !cc_audio_output_start(device->output)) {
        wm_audio_device_close(device);
        return NULL;
    }
    return device;
}

bool wm_audio_device_start(WmAudioDevice *device) {
    return device && cc_audio_output_start(device->output);
}

bool wm_audio_device_stop(WmAudioDevice *device) {
    return !device || cc_audio_output_stop(device->output);
}

void wm_audio_device_close(WmAudioDevice *device) {
    if (!device)
        return;
    cc_audio_output_close(device->output);
    free(device);
}
