#include "audio_internal.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool wm_audio_safe_name(const char *name) {
    if (!name || !*name || strlen(name) >= sizeof(((WmAudioClip *)0)->name))
        return false;
    for (const unsigned char *part = (const unsigned char *)name; *part; part++) {
        if (!((*part >= 'A' && *part <= 'Z') || (*part >= 'a' && *part <= 'z') ||
              (*part >= '0' && *part <= '9') || *part == '_' || *part == '-'))
            return false;
    }
    return true;
}

static bool path_for(char path[4096], const char *assets, const char *directory,
                     const char *name) {
    int length = snprintf(path, 4096, "%s/%s/%s.wav", assets, directory, name);
    return length > 0 && length < 4096;
}

static float json_number(const WmJson *json, size_t token, float fallback) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_NUMBER)
        return fallback;
    size_t length = json->tokens[token].end - json->tokens[token].start;
    if (length == 0 || length >= 64)
        return fallback;
    char buffer[64];
    memcpy(buffer, json->source + json->tokens[token].start, length);
    buffer[length] = '\0';
    char *end;
    float value = strtof(buffer, &end);
    return end != buffer && *end == '\0' && isfinite(value) ? value : fallback;
}

size_t wm_audio_manifest_entry(const WmAudio *audio, const char *name,
                               const WmJson **owner) {
    const WmJson *manifests[] = {&audio->sequence_manifest, &audio->direct_manifest};
    for (size_t index = 0; index < sizeof(manifests) / sizeof(manifests[0]); index++) {
        const WmJson *manifest = manifests[index];
        if (!manifest->count)
            continue;
        size_t entry = wm_json_member(manifest, 0, name);
        if (entry != WM_JSON_INVALID) {
            *owner = manifest;
            return entry;
        }
    }
    return WM_JSON_INVALID;
}

static bool read_table(const WmJson *json, size_t token, float *values, size_t count,
                       float maximum) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_ARRAY ||
        json->tokens[token].children != count)
        return false;
    for (size_t index = 0; index < count; index++) {
        float value = json_number(json, wm_json_index(json, token, index), NAN);
        if (!isfinite(value) || value < 0 || value > maximum)
            return false;
        values[index] = value;
    }
    return true;
}

static bool read_held_profile(const WmJson *json, size_t token,
                              WmAudioHeldProfile *profile) {
    if (token >= json->count || json->tokens[token].type != WM_JSON_OBJECT)
        return false;
    profile->attack_multiplier =
        json_number(json, wm_json_member(json, token, "attackMultiplier"), NAN);
    profile->decay_rate =
        json_number(json, wm_json_member(json, token, "decayRate"), NAN);
    profile->sustain_level =
        json_number(json, wm_json_member(json, token, "sustainLevel"), NAN);
    profile->release_rate =
        json_number(json, wm_json_member(json, token, "releaseRate"), NAN);
    profile->volume = json_number(json, wm_json_member(json, token, "volume"), NAN);
    profile->pan = json_number(json, wm_json_member(json, token, "pan"), NAN);
    return isfinite(profile->attack_multiplier) && profile->attack_multiplier >= 0 &&
           profile->attack_multiplier < 1 && isfinite(profile->decay_rate) &&
           profile->decay_rate > 0 && profile->decay_rate <= 65535 &&
           isfinite(profile->sustain_level) && profile->sustain_level >= -904 &&
           profile->sustain_level <= 0 && isfinite(profile->release_rate) &&
           profile->release_rate > 0 && profile->release_rate <= 65535 &&
           isfinite(profile->volume) && profile->volume >= 0 && profile->volume <= 1 &&
           isfinite(profile->pan) && profile->pan >= -1 && profile->pan <= 1;
}

void wm_audio_load_held_profiles(WmAudio *audio) {
    char path[4096];
    int length = snprintf(path, sizeof(path), "%s/audio-held.json", audio->assets);
    WmJson json = {0};
    if (length <= 0 || length >= (int)sizeof(path) ||
        !wm_json_load(&json, path, 128 * 1024))
        return;
    int schema = 0, rate = 0, block = 0;
    size_t tables = wm_json_member(&json, 0, "tables");
    size_t profiles = wm_json_member(&json, 0, "profiles");
    audio->has_held_profiles =
        wm_json_integer(&json, wm_json_member(&json, 0, "schema"), &schema) &&
        schema == 1 &&
        wm_json_integer(&json, wm_json_member(&json, 0, "sampleRate"), &rate) &&
        rate == WM_AUDIO_HELD_RATE &&
        wm_json_integer(&json, wm_json_member(&json, 0, "blockFrames"), &block) &&
        block == WM_AUDIO_HELD_BLOCK &&
        read_table(&json, wm_json_member(&json, tables, "decibels"),
                   audio->held_tables.decibels, WM_AUDIO_HELD_DECIBELS, 2) &&
        read_table(&json, wm_json_member(&json, tables, "pan"), audio->held_tables.pan,
                   WM_AUDIO_HELD_PAN, 2) &&
        wm_audio_held_tables_valid(&audio->held_tables) &&
        read_held_profile(&json, wm_json_member(&json, profiles, "WIPL_SE_CH_DRAG"),
                          &audio->held_profiles[0]) &&
        read_held_profile(&json, wm_json_member(&json, profiles, "WIPL_SE_BOARD_DRAG"),
                          &audio->held_profiles[1]);
    if (!audio->has_held_profiles)
        fprintf(stderr,
                "Invalid held-audio metadata; drag playback remains approximate.\n");
    wm_json_free(&json);
}

WmAudioClip *wm_audio_load_clip(WmAudio *audio, const char *name,
                                const char *directory) {
    if (!audio || !wm_audio_safe_name(name))
        return NULL;
    for (size_t index = 0; index < audio->clip_count; index++) {
        if (strcmp(audio->clips[index].name, name) == 0)
            return audio->clips[index].missing ? NULL : &audio->clips[index];
    }
    if (audio->clip_count == WM_AUDIO_MAX_CLIPS)
        return NULL;
    WmAudioClip *clip = &audio->clips[audio->clip_count++];
    strcpy(clip->name, name);
    const WmJson *manifest = NULL;
    size_t entry = strcmp(directory, "audio") == 0
                       ? wm_audio_manifest_entry(audio, name, &manifest)
                       : WM_JSON_INVALID;
    char path[4096], error[128];
    bool loaded = path_for(path, audio->assets, directory, name) &&
                  wm_audio_wav_read(path, &clip->pcm, error, sizeof(error));
    if (!loaded && entry != WM_JSON_INVALID) {
        char source[sizeof(clip->name)];
        size_t token = wm_json_member(manifest, entry, "sourceSymbol");
        if (wm_json_copy_text(manifest, token, source, sizeof(source)) &&
            wm_audio_safe_name(source) && strcmp(source, name) != 0 &&
            path_for(path, audio->assets, directory, source)) {
            loaded = wm_audio_wav_read(path, &clip->pcm, error, sizeof(error));
        }
    }
    if (!loaded) {
        clip->missing = true;
        fprintf(stderr, "Audio cue unavailable: %s\n", name);
        return NULL;
    }
    clip->gain = 1.0f;
    if (entry != WM_JSON_INVALID) {
        clip->gain =
            json_number(manifest, wm_json_member(manifest, entry, "gain"), 1.0f);
        float loop_start =
            json_number(manifest, wm_json_member(manifest, entry, "loopStart"), -1.0f);
        float loop_end =
            json_number(manifest, wm_json_member(manifest, entry, "loopEnd"), -1.0f);
        size_t loop_flag = wm_json_member(manifest, entry, "loop");
        if (loop_flag != WM_JSON_INVALID &&
            manifest->tokens[loop_flag].type == WM_JSON_BOOLEAN &&
            manifest->source[manifest->tokens[loop_flag].start] == 't' &&
            loop_start >= 0.0f && loop_end > loop_start &&
            (double)loop_end * clip->pcm.sample_rate <
                (double)clip->pcm.frame_count + 1.0) {
            /* Round the same float products as before, but validate the
             * rounded values before narrowing to uint32_t. */
            float first_frame = roundf(loop_start * clip->pcm.sample_rate);
            float end_frame = roundf(loop_end * clip->pcm.sample_rate);
            if (isfinite(first_frame) && isfinite(end_frame) && first_frame >= 0.0f &&
                first_frame < end_frame && (double)end_frame <= clip->pcm.frame_count) {
                clip->pcm.looping = true;
                clip->pcm.loop_start = (uint32_t)first_frame;
                clip->pcm.loop_end = (uint32_t)end_frame;
            }
        }
        size_t symbol = wm_json_member(manifest, entry, "sourceSymbol");
        int profile = wm_json_equals(manifest, symbol, "WIPL_SE_CH_DRAG")      ? 0
                      : wm_json_equals(manifest, symbol, "WIPL_SE_BOARD_DRAG") ? 1
                                                                               : -1;
        if (profile >= 0 && clip->pcm.looping && clip->pcm.channels == 1 &&
            clip->pcm.sample_rate == WM_AUDIO_HELD_RATE) {
            if (audio->has_held_profiles) {
                clip->held_profile = &audio->held_profiles[profile];
            } else if (!audio->warned_held_profiles) {
                fprintf(stderr, "Drag envelope metadata unavailable; re-export "
                                "audio for held-voice playback.\n");
                audio->warned_held_profiles = true;
            }
        }
    }
    /* Leave headroom when the maximum voice count is mixed at up to 2x gain. */
    const float maximum_gain = FLT_MAX / (WM_AUDIO_MAX_VOICES * 4.0f);
    if (!isfinite(clip->gain) || clip->gain < 0.0f || clip->gain > maximum_gain)
        clip->gain = 1.0f;
    return clip;
}
