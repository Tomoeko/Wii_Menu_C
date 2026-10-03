#include "app_recording.h"

#include <assert.h>
#include <string.h>

struct WmAudio {
    bool available;
    bool running;
    bool capturing;
    bool fail_start;
    bool overflow;
    unsigned starts;
    unsigned stops;
};

struct CcRecording {
    CcRecordingAudioSource audio;
    bool started;
};

static CcRecording instance;
static bool fail_open;
static unsigned opens;
static unsigned closes;
static bool expected_half;
static CcCaptureAudioMode expected_audio;
static bool supported_audio = true;

bool cc_capture_audio_mode_supported(CcCaptureAudioMode mode) {
    return supported_audio &&
           (mode == CC_CAPTURE_AUDIO_NORMAL || mode == CC_CAPTURE_AUDIO_WEB);
}

bool wm_audio_output_stop(WmAudio *audio) {
    if (audio) {
        audio->running = false;
        audio->stops++;
    }
    return true;
}

bool wm_audio_output_start(WmAudio *audio) {
    assert(audio && audio->available && audio->capturing);
    audio->starts++;
    audio->running = !audio->fail_start;
    return audio->running;
}

bool wm_audio_output_available(const WmAudio *audio) {
    return audio && audio->available;
}

bool wm_audio_capture_begin(WmAudio *audio, size_t capacity_frames) {
    assert(audio && !audio->running && !audio->capturing);
    assert(capacity_frames == 96000);
    audio->capturing = true;
    return true;
}

size_t wm_audio_capture_read(WmAudio *audio, float *stereo, size_t capacity_frames) {
    assert(audio && audio->capturing && stereo && capacity_frames);
    stereo[0] = 0.25f;
    stereo[1] = -0.75f;
    return 1;
}

bool wm_audio_capture_failed(const WmAudio *audio) {
    return audio->overflow;
}

void wm_audio_capture_end(WmAudio *audio) {
    assert(audio && !audio->running && audio->capturing);
    audio->capturing = false;
}

CcRecording *cc_recording_open(const CcRecordingOptions *options) {
    assert(options->platform);
    assert(options->sample_rate == 48000 && options->video_rate == 60);
    assert(options->half_size == expected_half);
    assert(options->audio_mode == expected_audio);
    assert(strcmp(options->filename_prefix, "Wii") == 0);
    opens++;
    if (fail_open)
        return NULL;
    instance = (CcRecording){.audio = options->audio};
    if (instance.audio.begin)
        assert(instance.audio.begin(instance.audio.context, 96000));
    else
        assert(!instance.audio.read && !instance.audio.failed && !instance.audio.end);
    return &instance;
}

bool cc_recording_audio_start(CcRecording *recording, double now) {
    assert(recording == &instance && now == 12.5);
    if (recording->audio.context)
        assert(((WmAudio *)recording->audio.context)->running);
    recording->started = true;
    return true;
}

bool cc_recording_close(CcRecording *recording, double now) {
    assert(recording == &instance && now == 14.0);
    if (recording->audio.end)
        recording->audio.end(recording->audio.context);
    closes++;
    return true;
}

int main(void) {
    unsigned char platform_storage;
    WmPlatform *platform = (WmPlatform *)&platform_storage;
    WmAudio audio = {.available = true, .running = true};
    CcRecording *recording =
        wm_app_recording_open(platform, &audio, expected_half, expected_audio);
    assert(recording && !audio.running && audio.capturing);
    assert(wm_app_recording_start(recording, &audio, 12.5));
    assert(audio.running && audio.starts == 1 && instance.started);
    float stereo[2];
    assert(instance.audio.read(instance.audio.context, stereo, 1) == 1);
    assert(stereo[0] == 0.25f && stereo[1] == -0.75f);
    assert(!instance.audio.failed(instance.audio.context));
    audio.overflow = true;
    assert(instance.audio.failed(instance.audio.context));
    assert(wm_app_recording_close(recording, &audio, 14.0));
    assert(!audio.running && !audio.capturing && closes == 1);

    expected_half = true;
    expected_audio = CC_CAPTURE_AUDIO_WEB;
    recording = wm_app_recording_open(platform, &audio, expected_half, expected_audio);
    assert(recording && !audio.running && audio.capturing);
    assert(wm_app_recording_start(recording, &audio, 12.5));
    float half_stereo[2];
    assert(instance.audio.read(instance.audio.context, half_stereo, 1) == 1);
    assert(memcmp(stereo, half_stereo, sizeof(stereo)) == 0);
    assert(wm_app_recording_close(recording, &audio, 14.0));
    expected_half = false;
    expected_audio = CC_CAPTURE_AUDIO_NORMAL;

    recording = wm_app_recording_open(platform, NULL, expected_half, expected_audio);
    assert(recording && !instance.audio.context);
    assert(wm_app_recording_start(recording, NULL, 12.5));
    assert(wm_app_recording_close(recording, NULL, 14.0));
    WmAudio unavailable = {0};
    recording =
        wm_app_recording_open(platform, &unavailable, expected_half, expected_audio);
    assert(recording && !instance.audio.begin);
    assert(wm_app_recording_start(recording, &unavailable, 12.5));
    assert(wm_app_recording_close(recording, &unavailable, 14.0));

    audio = (WmAudio){.available = true, .running = true, .fail_start = true};
    recording = wm_app_recording_open(platform, &audio, expected_half, expected_audio);
    assert(!wm_app_recording_start(recording, &audio, 12.5));
    assert(!instance.started && !audio.running);
    assert(wm_app_recording_close(recording, &audio, 14.0));
    fail_open = true;
    audio.running = true;
    assert(!wm_app_recording_open(platform, &audio, expected_half, expected_audio));
    assert(!audio.running && !audio.capturing && opens == 6);
    audio.running = true;
    unsigned stops = audio.stops;
    supported_audio = false;
    assert(!wm_app_recording_open(platform, &audio, false, CC_CAPTURE_AUDIO_WEB));
    assert(audio.running && audio.stops == stops && opens == 6);
    assert(wm_app_recording_start(NULL, &audio, 0.0));
    assert(wm_app_recording_close(NULL, &audio, 0.0));
    return 0;
}
