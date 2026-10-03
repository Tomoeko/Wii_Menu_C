#include "app_recording.h"

static bool begin_audio(void *context, size_t capacity_frames) {
    return wm_audio_capture_begin(context, capacity_frames);
}

static size_t read_audio(void *context, float *stereo, size_t capacity_frames) {
    return wm_audio_capture_read(context, stereo, capacity_frames);
}

static bool failed_audio(void *context) {
    return wm_audio_capture_failed(context);
}

static void end_audio(void *context) {
    wm_audio_capture_end(context);
}

CcRecording *wm_app_recording_open(WmPlatform *platform, WmAudio *audio, bool half_size,
                                   CcCaptureAudioMode audio_mode) {
    if (!cc_capture_audio_mode_supported(audio_mode) || !wm_audio_output_stop(audio))
        return NULL;
    CcRecordingOptions options = {.platform = platform,
                                  .sample_rate = 48000,
                                  .video_rate = 60,
                                  .half_size = half_size,
                                  .audio_mode = audio_mode,
                                  .filename_prefix = "Wii"};
    if (wm_audio_output_available(audio)) {
        options.audio = (CcRecordingAudioSource){.context = audio,
                                                 .begin = begin_audio,
                                                 .read = read_audio,
                                                 .failed = failed_audio,
                                                 .end = end_audio};
    }
    return cc_recording_open(&options);
}

bool wm_app_recording_start(CcRecording *recording, WmAudio *audio, double now) {
    if (!recording)
        return true;
    if (wm_audio_output_available(audio) && !wm_audio_output_start(audio))
        return false;
    return cc_recording_audio_start(recording, now);
}

bool wm_app_recording_close(CcRecording *recording, WmAudio *audio, double now) {
    if (!recording)
        return true;
    bool stopped = wm_audio_output_stop(audio);
    bool finalized = cc_recording_close(recording, now);
    return stopped && finalized;
}
