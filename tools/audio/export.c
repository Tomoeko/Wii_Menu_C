#define _POSIX_C_SOURCE 200809L

#include "wii_menu/audio/audio_wave.h"
#include "wii_menu/audio/audio_sequence.h"
#include "wii_menu/resources/resource_rsar.h"
#include "wii_menu/resources/resource_u8.h"
#include "wii_menu/support/regular_file.h"

#include "atomic_file.h"
#include "export_directory.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct SoundName {
    const char *name;
    const char *symbol;
} SoundName;

static const SoundName direct_sounds[] = {
    {"backgroundIntro", "WIPL_SE_WII_START"},
    {"page", "WSD_SELECT"},
    {"discPreview", "WIPL_ME_NO_DISC_BANNER"},
    {"WSD_SELECT", "WSD_SELECT"},
    {"WIPL_SE_CALENDAR_SCROLL", "WIPL_SE_CALENDAR_SCROLL"},
    {"WIPL_SE_BOARD_SELECT", "WIPL_SE_BOARD_SELECT"},
    {"WIPL_ME_VIRTUAL_CONSOLE", "WIPL_ME_VIRTUAL_CONSOLE"},
    {"WIPL_SE_WII_START", "WIPL_SE_WII_START"},
    {"WIPL_ME_NO_DISC_BANNER", "WIPL_ME_NO_DISC_BANNER"},
    {"WIPL_SE_COPY_FINISH", "WIPL_SE_COPY_FINISH"},
    {"WIPL_ME_GC_BANNER", "WIPL_ME_GC_BANNER"},
    {"WIPL_ME_INVALID_DISC_BANNER", "WIPL_ME_INVALID_DISC_BANNER"},
    {"WIPL_ME_SD_BANNER", "WIPL_ME_SD_BANNER"},
    {"WIPL_SE_SK_PAGE_CHG", "WIPL_SE_SK_PAGE_CHG"}};

static const SoundName sequence_aliases[] = {{"background", "WIPL_BGM_MENU"},
                                             {"hover", "WIPL_SE_CH_TARGETTING"},
                                             {"buttonHover", "WIPL_SE_BT_TARGETTING"},
                                             {"select", "WIPL_SE_CH_SELECT"},
                                             {"click", "WIPL_SE_BT_PUSH"},
                                             {"back", "WIPL_SE_CH_UNSELECT"},
                                             {"confirm", "WIPL_SE_DECIDE"},
                                             {"cancel", "WIPL_SE_CANCEL"},
                                             {"balloon", "WIPL_SE_BALLOON"},
                                             {"infoWindow", "WIPL_SE_INFO_WINDOW"},
                                             {"grab", "WIPL_SE_CH_HOLD"},
                                             {"drop", "WIPL_SE_CH_SET"},
                                             {"invalidDrop", "WIPL_SE_CH_NOT_MOVE"},
                                             {"drag", "WIPL_SE_CH_DRAG"},
                                             {"dateSelect", "WIPL_SE_DATE_SELECT"}};

static const SoundName speaker_sounds[] = {{"HOME_SPEAKER_CONNECT1", "connect1.bwav"},
                                           {"HOME_SPEAKER_CONNECT2", "connect2.bwav"},
                                           {"HOME_SPEAKER_CONNECT3", "connect3.bwav"},
                                           {"HOME_SPEAKER_CONNECT4", "connect4.bwav"},
                                           {"HOME_SPEAKER_VOLUME", "volume.bwav"}};

static bool combine_path(char path[4096], const char *root, const char *middle,
                         const char *name) {
    int length = snprintf(path, 4096, "%s/%s%s", root, middle, name);
    return length > 0 && length < 4096;
}

static int usage(const char *program) {
    fprintf(stderr, "Usage: %s RESOURCE_97.app EXECUTABLE_98.app OUTPUT_ASSETS\n",
            program);
    fprintf(stderr, "Exports local original waves and sequenced menu audio.\n");
    return 2;
}

static bool audio_filename(char destination[4096], const char *output,
                           const char *name) {
    for (const unsigned char *part = (const unsigned char *)name; *part; part++) {
        if (!((*part >= 'A' && *part <= 'Z') || (*part >= 'a' && *part <= 'z') ||
              (*part >= '0' && *part <= '9') || *part == '_' || *part == '-'))
            return false;
    }
    char filename[144];
    int length = snprintf(filename, sizeof(filename), "%s.wav", name);
    return length > 4 && length < (int)sizeof(filename) &&
           combine_path(destination, output, "audio/", filename);
}

static bool write_sound_entry(FILE *manifest, const char *name, const char *symbol,
                              const WmAudioPcm *pcm, double gain, bool first) {
    if (!first && fputs(",\n", manifest) < 0)
        return false;
    return fprintf(manifest,
                   "  \"%s\": {\"sourceSymbol\": \"%s\", "
                   "\"gain\": %.9g, \"loop\": %s, "
                   "\"loopStart\": %.9g, \"loopEnd\": %.9g}",
                   name, symbol, gain, pcm->looping ? "true" : "false",
                   (double)pcm->loop_start / pcm->sample_rate,
                   (double)pcm->loop_end / pcm->sample_rate) > 0;
}

static bool held_symbol(const char *symbol) {
    return strcmp(symbol, "WIPL_SE_CH_DRAG") == 0 ||
           strcmp(symbol, "WIPL_SE_BOARD_DRAG") == 0;
}

static bool write_float_array(FILE *file, const float *values, size_t count) {
    for (size_t index = 0; index < count; index++) {
        if (fprintf(file, "      %.9g%s\n", (double)values[index],
                    index + 1 < count ? "," : "") < 0)
            return false;
    }
    return true;
}

static bool write_held_profile(FILE *file, const char *symbol,
                               const WmAudioHeldProfile *profile, bool first) {
    if (!first && fputs(",\n", file) < 0)
        return false;
    return fprintf(file,
                   "    \"%s\": {\n"
                   "      \"attackMultiplier\": %.9g,\n"
                   "      \"decayRate\": %.9g,\n"
                   "      \"sustainLevel\": %.9g,\n"
                   "      \"releaseRate\": %.9g,\n"
                   "      \"volume\": %.9g,\n"
                   "      \"pan\": %.9g\n"
                   "    }",
                   symbol, (double)profile->attack_multiplier,
                   (double)profile->decay_rate, (double)profile->sustain_level,
                   (double)profile->release_rate, (double)profile->volume,
                   (double)profile->pan) > 0;
}

static bool export_held_manifest(const WmRsar *archive, const uint8_t *executable,
                                 size_t executable_size, const char *output) {
    static const char *const symbols[] = {"WIPL_SE_CH_DRAG", "WIPL_SE_BOARD_DRAG"};
    WmAudioHeldProfile profiles[2];
    WmAudioHeldTables tables;
    for (size_t index = 0; index < 2; index++) {
        WmRsarSound sound;
        WmAudioPcm wave = {0};
        char error[160] = {0};
        if (!wm_rsar_find_sound(archive, symbols[index], &sound) ||
            !wm_sequence_extract_held(archive, &sound, executable, executable_size,
                                      &profiles[index], &tables, &wave, error,
                                      sizeof(error))) {
            fprintf(stderr, "Unsupported held sequence %s: %s\n", symbols[index],
                    error);
            return false;
        }
        wm_audio_pcm_free(&wave);
    }
    char path[4096];
    if (!combine_path(path, output, "", "audio-held.json"))
        return false;
    WmAtomicFile temporary;
    if (wm_atomic_file_open(&temporary, path) != WM_ATOMIC_FILE_OK)
        return false;
    FILE *file = temporary.stream;
    bool valid = fputs("{\n"
                       "  \"schema\": 1,\n"
                       "  \"sampleRate\": 32000,\n"
                       "  \"blockFrames\": 96,\n"
                       "  \"tables\": {\n"
                       "    \"decibels\": [\n",
                       file) >= 0;
    if (valid)
        valid = write_float_array(file, tables.decibels, WM_AUDIO_HELD_DECIBELS);
    if (valid)
        valid = fputs("    ],\n    \"pan\": [\n", file) >= 0;
    if (valid)
        valid = write_float_array(file, tables.pan, WM_AUDIO_HELD_PAN);
    if (valid)
        valid = fputs("    ]\n  },\n  \"profiles\": {\n", file) >= 0;
    for (size_t index = 0; index < 2 && valid; index++) {
        valid = write_held_profile(file, symbols[index], &profiles[index], index == 0);
    }
    if (valid)
        valid = fputs("\n  }\n}\n", file) >= 0;
    if (valid)
        valid = wm_atomic_file_commit(&temporary, path);
    else
        wm_atomic_file_discard(&temporary);
    return valid;
}

static bool export_sequences(const WmRsar *archive, const uint8_t *executable,
                             size_t executable_size, const char *output) {
    if (!export_held_manifest(archive, executable, executable_size, output))
        return false;
    char manifest_path[4096];
    if (!combine_path(manifest_path, output, "", "audio-sequence.json"))
        return false;
    WmAtomicFile temporary;
    if (wm_atomic_file_open(&temporary, manifest_path) != WM_ATOMIC_FILE_OK)
        return false;
    FILE *manifest = temporary.stream;
    bool valid = fputs("{\n", manifest) >= 0;
    size_t rendered = 0, skipped = 0, aliases = 0;
    for (size_t index = 0; index < wm_rsar_sound_count(archive) && valid; index++) {
        char symbol[128], destination[4096], error[160] = {0};
        WmRsarSound sound;
        if (!wm_rsar_sound_at(archive, index, symbol, sizeof(symbol), &sound)) {
            valid = false;
            break;
        }
        if (sound.type != 1)
            continue;
        WmAudioPcm pcm = {0};
        float gain = 1.0f;
        bool held = held_symbol(symbol);
        bool decoded;
        if (held) {
            WmAudioHeldProfile profile;
            WmAudioHeldTables tables;
            decoded =
                wm_sequence_extract_held(archive, &sound, executable, executable_size,
                                         &profile, &tables, &pcm, error, sizeof(error));
            gain = (float)sound.volume / 127.0f;
        } else {
            decoded = wm_sequence_render(archive, &sound, executable, executable_size,
                                         &pcm, error, sizeof(error));
        }
        if (!decoded) {
            fprintf(stderr, "Unsupported sequence %s: %s\n", symbol, error);
            skipped++;
            continue;
        }
        if (!audio_filename(destination, output, symbol) ||
            !wm_audio_wav_write(destination, &pcm, error, sizeof(error)) ||
            !write_sound_entry(manifest, symbol, symbol, &pcm, gain,
                               rendered + aliases == 0)) {
            fprintf(stderr, "Could not export sequence %s: %s\n", symbol, error);
            wm_audio_pcm_free(&pcm);
            valid = false;
            break;
        }
        rendered++;
        for (size_t alias = 0;
             alias < sizeof(sequence_aliases) / sizeof(sequence_aliases[0]); alias++) {
            if (strcmp(sequence_aliases[alias].symbol, symbol) != 0)
                continue;
            /* Alias metadata points at the already exported source WAV. */
            if (!write_sound_entry(manifest, sequence_aliases[alias].name, symbol, &pcm,
                                   gain, false)) {
                fprintf(stderr, "Could not record sequence alias %s.\n",
                        sequence_aliases[alias].name);
                valid = false;
                break;
            }
            aliases++;
        }
        wm_audio_pcm_free(&pcm);
    }
    if (valid)
        valid = fputs("\n}\n", manifest) >= 0;
    if (valid)
        valid = wm_atomic_file_commit(&temporary, manifest_path);
    else
        wm_atomic_file_discard(&temporary);
    if (!valid)
        return false;
    printf("Exported %zu sequenced sounds and %zu aliases; %zu unsupported.\n",
           rendered, aliases, skipped);
    return skipped == 0;
}

static bool export_speaker_samples(const WmU8Archive *container, const char *output,
                                   FILE *manifest, size_t *entries) {
    const WmU8Entry *speaker = wm_u8_find(container, "homebutton/SpeakerSe.arc");
    if (!speaker) {
        fprintf(stderr, "Missing HOME remote-speaker archive.\n");
        return false;
    }
    WmU8Archive samples = {0};
    char error[160];
    if (!wm_u8_parse(speaker->data, speaker->size, &samples, error, sizeof(error))) {
        fprintf(stderr, "Could not open remote-speaker samples: %s\n", error);
        return false;
    }
    bool valid = true;
    for (size_t index = 0;
         index < sizeof(speaker_sounds) / sizeof(speaker_sounds[0]) && valid; index++) {
        const WmU8Entry *entry = wm_u8_find(&samples, speaker_sounds[index].symbol);
        if (!entry || !entry->size || (entry->size & 1) || entry->size / 2 > 20000000) {
            fprintf(stderr, "Invalid remote-speaker sample %s.\n",
                    speaker_sounds[index].symbol);
            valid = false;
            break;
        }
        WmAudioPcm pcm = {.samples = malloc(entry->size),
                          .sample_rate = 6000,
                          .frame_count = (uint32_t)(entry->size / 2),
                          .channels = 1};
        if (!pcm.samples) {
            valid = false;
            break;
        }
        for (size_t frame = 0; frame < pcm.frame_count; frame++) {
            const uint8_t *source = entry->data + frame * 2;
            pcm.samples[frame] = (int16_t)(((uint16_t)source[0] << 8) | source[1]);
        }
        char destination[4096];
        if (!audio_filename(destination, output, speaker_sounds[index].name) ||
            !wm_audio_wav_write(destination, &pcm, error, sizeof(error))) {
            fprintf(stderr, "Could not export remote-speaker sample: %s\n", error);
            valid = false;
        } else {
            if (*entries > 0)
                valid = fputs(",\n", manifest) >= 0;
            if (valid)
                valid =
                    fprintf(manifest,
                            "  \"%s\": {\"sourceSymbol\": \"%s\", "
                            "\"gain\": 1, \"loop\": false}",
                            speaker_sounds[index].name, speaker_sounds[index].name) > 0;
            (*entries)++;
        }
        wm_audio_pcm_free(&pcm);
    }
    wm_u8_free(&samples);
    return valid;
}

static bool export_direct_waves(const WmRsar *archive, const char *output,
                                FILE *manifest, size_t *exported) {
    for (size_t index = 0; index < sizeof(direct_sounds) / sizeof(direct_sounds[0]);
         index++) {
        const SoundName *source = &direct_sounds[index];
        WmRsarSound sound;
        WmAudioPcm pcm = {0};
        char error[160] = {0};
        if (!wm_rsar_find_sound(archive, source->symbol, &sound) ||
            !wm_rsar_decode_direct_wave(archive, &sound, &pcm, error, sizeof(error))) {
            fprintf(stderr, "Could not decode %s: %s\n", source->symbol, error);
            wm_audio_pcm_free(&pcm);
            return false;
        }
        char destination[4096];
        if (!audio_filename(destination, output, source->name) ||
            !wm_audio_wav_write(destination, &pcm, error, sizeof(error))) {
            fprintf(stderr, "Could not export %s: %s\n", source->symbol,
                    error[0] ? error : "invalid audio output path");
            wm_audio_pcm_free(&pcm);
            return false;
        }
        bool valid = write_sound_entry(manifest, source->name, source->symbol, &pcm,
                                       (double)sound.volume / 127.0, *exported == 0);
        wm_audio_pcm_free(&pcm);
        if (!valid)
            return false;
        (*exported)++;
    }
    return true;
}

static bool export_direct_manifest(const WmU8Archive *container, const WmRsar *archive,
                                   const char *output, size_t *exported) {
    char audio_directory[4096];
    if (!wm_export_directory_root(output, 0755) ||
        !combine_path(audio_directory, output, "", "audio") ||
        !wm_export_directory_child(output, "audio", 0755)) {
        fprintf(stderr, "Could not create local audio directory.\n");
        return false;
    }
    char manifest_path[4096];
    if (!combine_path(manifest_path, output, "", "audio-direct.json"))
        return false;
    WmAtomicFile temporary;
    if (wm_atomic_file_open(&temporary, manifest_path) != WM_ATOMIC_FILE_OK) {
        fprintf(stderr, "Could not create local audio manifest.\n");
        return false;
    }
    FILE *manifest = temporary.stream;
    bool valid = fputs("{\n", manifest) >= 0;
    if (valid)
        valid = export_direct_waves(archive, output, manifest, exported);
    if (valid)
        valid = export_speaker_samples(container, output, manifest, exported);
    if (valid)
        valid = fputs("\n}\n", manifest) >= 0;
    if (valid)
        valid = wm_atomic_file_commit(&temporary, manifest_path);
    else
        wm_atomic_file_discard(&temporary);
    return valid;
}

int main(int argc, char **argv) {
    if (argc != 4)
        return usage(argv[0]);

    uint8_t *source = NULL;
    uint8_t *executable = NULL;
    size_t source_size = 0;
    size_t executable_size = 0;
    WmU8Archive container = {0};
    WmRsar archive = {0};
    char error[160] = {0};
    int result = 1;

    if (wm_regular_file_read_bytes(argv[1], 64, 128 * 1024 * 1024, &source,
                                   &source_size) != WM_REGULAR_FILE_OK) {
        fprintf(stderr, "Could not read bounded WAD resource content.\n");
        goto release_inputs;
    }
    if (wm_regular_file_read_bytes(argv[2], 64, 128 * 1024 * 1024, &executable,
                                   &executable_size) != WM_REGULAR_FILE_OK) {
        fprintf(stderr, "Could not read bounded System Menu executable.\n");
        goto release_inputs;
    }
    if (!wm_u8_parse(source, source_size, &container, error, sizeof(error))) {
        fprintf(stderr, "Could not open resource content: %s\n", error);
        goto release_inputs;
    }
    const WmU8Entry *entry = wm_u8_find(&container, "sound/IplSound.brsar");
    if (!entry ||
        !wm_rsar_open(entry->data, entry->size, &archive, error, sizeof(error))) {
        fprintf(stderr, "Could not open IplSound archive: %s\n",
                entry ? error : "missing sound/IplSound.brsar");
        goto release_inputs;
    }

    size_t exported = 0;
    if (!export_direct_manifest(&container, &archive, argv[3], &exported))
        goto release_inputs;
    printf("Exported %zu original direct-wave and remote-speaker sounds.\n", exported);
    result = export_sequences(&archive, executable, executable_size, argv[3]) ? 0 : 1;

release_inputs:
    wm_u8_free(&container);
    free(source);
    free(executable);
    return result;
}
