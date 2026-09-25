#define _POSIX_C_SOURCE 200809L

#include "wii_menu/audio_wave.h"
#include "wii_menu/audio_sequence.h"
#include "wii_menu/resource_rsar.h"
#include "wii_menu/resource_u8.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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
    {"WIPL_SE_SK_PAGE_CHG", "WIPL_SE_SK_PAGE_CHG"}
};

static const SoundName sequence_aliases[] = {
    {"background", "WIPL_BGM_MENU"},
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
    {"dateSelect", "WIPL_SE_DATE_SELECT"}
};

static const SoundName speaker_sounds[] = {
    {"HOME_SPEAKER_CONNECT1", "connect1.bwav"},
    {"HOME_SPEAKER_CONNECT2", "connect2.bwav"},
    {"HOME_SPEAKER_CONNECT3", "connect3.bwav"},
    {"HOME_SPEAKER_CONNECT4", "connect4.bwav"},
    {"HOME_SPEAKER_VOLUME", "volume.bwav"}
};

static bool make_directory(const char *path)
{
    if (mkdir(path, 0755) == 0) return true;
    if (errno != EEXIST) return false;
    struct stat info;
    return stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

static bool combine_path(char path[4096], const char *root,
                         const char *middle, const char *name)
{
    int length = snprintf(path, 4096, "%s/%s%s", root, middle, name);
    return length > 0 && length < 4096;
}

static uint8_t *read_file(const char *path, size_t *size)
{
    FILE *file = fopen(path, "rb");
    if (!file || fseek(file, 0, SEEK_END) != 0) {
        if (file) fclose(file);
        return NULL;
    }
    long length = ftell(file);
    if (length < 64 || length > 128 * 1024 * 1024 ||
        fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return NULL;
    }
    uint8_t *data = malloc((size_t)length);
    if (!data || fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        fclose(file);
        return NULL;
    }
    fclose(file);
    *size = (size_t)length;
    return data;
}

static int usage(const char *program)
{
    fprintf(stderr, "Usage: %s RESOURCE_97.app EXECUTABLE_98.app OUTPUT_ASSETS\n",
            program);
    fprintf(stderr, "Exports local original waves and sequenced menu audio.\n");
    return 2;
}

static bool audio_filename(char destination[4096], const char *output,
                           const char *name)
{
    for (const unsigned char *part = (const unsigned char *)name; *part; part++) {
        if (!((*part >= 'A' && *part <= 'Z') ||
              (*part >= 'a' && *part <= 'z') ||
              (*part >= '0' && *part <= '9') || *part == '_' || *part == '-'))
            return false;
    }
    char filename[144];
    int length = snprintf(filename, sizeof(filename), "%s.wav", name);
    return length > 4 && length < (int)sizeof(filename) &&
           combine_path(destination, output, "audio/", filename);
}

static bool write_sequence_entry(FILE *manifest, const char *name,
                                 const char *symbol, const WmAudioPcm *pcm,
                                 bool first)
{
    if (!first && fputs(",\n", manifest) < 0) return false;
    return fprintf(manifest,
                   "  \"%s\": {\"sourceSymbol\": \"%s\", "
                   "\"gain\": 1, \"loop\": %s, "
                   "\"loopStart\": %.9g, \"loopEnd\": %.9g}",
                   name, symbol, pcm->looping ? "true" : "false",
                   (double)pcm->loop_start / pcm->sample_rate,
                   (double)pcm->loop_end / pcm->sample_rate) > 0;
}

static bool export_sequences(const WmRsar *archive,
                             const uint8_t *executable, size_t executable_size,
                             const char *output)
{
    char manifest_path[4096];
    if (!combine_path(manifest_path, output, "", "audio-sequence.json"))
        return false;
    FILE *manifest = fopen(manifest_path, "wb");
    if (!manifest) return false;
    bool valid = fputs("{\n", manifest) >= 0;
    size_t rendered = 0, skipped = 0, aliases = 0;
    for (size_t index = 0;
         index < wm_rsar_sound_count(archive) && valid;
         index++) {
        char symbol[128], destination[4096], error[160] = {0};
        WmRsarSound sound;
        if (!wm_rsar_sound_at(archive, index, symbol,
                              sizeof(symbol), &sound)) {
            valid = false;
            break;
        }
        if (sound.type != 1) continue;
        WmAudioPcm pcm = {0};
        if (!wm_sequence_render(archive, &sound, executable,
                                executable_size, &pcm,
                                error, sizeof(error))) {
            fprintf(stderr, "Unsupported sequence %s: %s\n", symbol, error);
            skipped++;
            continue;
        }
        if (!audio_filename(destination, output, symbol) ||
            !wm_audio_wav_write(destination, &pcm, error, sizeof(error)) ||
            !write_sequence_entry(manifest, symbol, symbol, &pcm,
                                  rendered + aliases == 0)) {
            fprintf(stderr, "Could not export sequence %s: %s\n", symbol, error);
            wm_audio_pcm_free(&pcm);
            valid = false;
            break;
        }
        rendered++;
        for (size_t alias = 0;
             alias < sizeof(sequence_aliases) / sizeof(sequence_aliases[0]);
             alias++) {
            if (strcmp(sequence_aliases[alias].symbol, symbol) != 0) continue;
            if (!audio_filename(destination, output,
                                sequence_aliases[alias].name) ||
                !wm_audio_wav_write(destination, &pcm,
                                    error, sizeof(error)) ||
                !write_sequence_entry(manifest, sequence_aliases[alias].name,
                                      symbol, &pcm, false)) {
                fprintf(stderr, "Could not export sequence alias %s: %s\n",
                        sequence_aliases[alias].name, error);
                valid = false;
                break;
            }
            aliases++;
        }
        wm_audio_pcm_free(&pcm);
    }
    if (valid) valid = fputs("\n}\n", manifest) >= 0;
    if (fclose(manifest) != 0) valid = false;
    if (!valid) remove(manifest_path);
    if (!valid) return false;
    printf("Exported %zu sequenced sounds and %zu aliases; %zu unsupported.\n",
           rendered, aliases, skipped);
    return skipped == 0;
}

static bool export_speaker_samples(const WmU8Archive *container,
                                   const char *output, FILE *manifest,
                                   size_t *entries)
{
    const WmU8Entry *speaker = wm_u8_find(container, "homebutton/SpeakerSe.arc");
    if (!speaker) {
        fprintf(stderr, "Missing HOME remote-speaker archive.\n");
        return false;
    }
    WmU8Archive samples = {0};
    char error[160];
    if (!wm_u8_parse(speaker->data, speaker->size, &samples,
                     error, sizeof(error))) {
        fprintf(stderr, "Could not open remote-speaker samples: %s\n", error);
        return false;
    }
    bool valid = true;
    for (size_t index = 0;
         index < sizeof(speaker_sounds) / sizeof(speaker_sounds[0]) && valid;
         index++) {
        const WmU8Entry *entry = wm_u8_find(&samples,
                                             speaker_sounds[index].symbol);
        if (!entry || !entry->size || (entry->size & 1) ||
            entry->size / 2 > 20000000) {
            fprintf(stderr, "Invalid remote-speaker sample %s.\n",
                    speaker_sounds[index].symbol);
            valid = false;
            break;
        }
        WmAudioPcm pcm = {
            .samples = malloc(entry->size),
            .sample_rate = 6000,
            .frame_count = (uint32_t)(entry->size / 2),
            .channels = 1
        };
        if (!pcm.samples) { valid = false; break; }
        for (size_t frame = 0; frame < pcm.frame_count; frame++) {
            const uint8_t *source = entry->data + frame * 2;
            pcm.samples[frame] = (int16_t)(((uint16_t)source[0] << 8) |
                                           source[1]);
        }
        char destination[4096];
        if (!audio_filename(destination, output, speaker_sounds[index].name) ||
            !wm_audio_wav_write(destination, &pcm, error, sizeof(error))) {
            fprintf(stderr, "Could not export remote-speaker sample: %s\n", error);
            valid = false;
        } else {
            if (*entries > 0) valid = fputs(",\n", manifest) >= 0;
            if (valid) valid = fprintf(manifest,
                                       "  \"%s\": {\"sourceSymbol\": \"%s\", "
                                       "\"gain\": 1, \"loop\": false}",
                                       speaker_sounds[index].name,
                                       speaker_sounds[index].name) > 0;
            (*entries)++;
        }
        wm_audio_pcm_free(&pcm);
    }
    wm_u8_free(&samples);
    return valid;
}

int main(int argc, char **argv)
{
    if (argc != 4) return usage(argv[0]);
    size_t source_size;
    uint8_t *source = read_file(argv[1], &source_size);
    if (!source) {
        fprintf(stderr, "Could not read bounded WAD resource content.\n");
        return 1;
    }
    size_t executable_size;
    uint8_t *executable = read_file(argv[2], &executable_size);
    if (!executable) {
        fprintf(stderr, "Could not read bounded System Menu executable.\n");
        free(source);
        return 1;
    }
    WmU8Archive container = {0};
    char error[160];
    if (!wm_u8_parse(source, source_size, &container, error, sizeof(error))) {
        fprintf(stderr, "Could not open resource content: %s\n", error);
        free(source);
        free(executable);
        return 1;
    }
    const WmU8Entry *entry = wm_u8_find(&container, "sound/IplSound.brsar");
    WmRsar archive;
    if (!entry || !wm_rsar_open(entry->data, entry->size, &archive,
                                error, sizeof(error))) {
        fprintf(stderr, "Could not open IplSound archive: %s\n",
                entry ? error : "missing sound/IplSound.brsar");
        wm_u8_free(&container);
        free(source);
        free(executable);
        return 1;
    }
    char audio_directory[4096];
    if (!make_directory(argv[3]) ||
        !combine_path(audio_directory, argv[3], "", "audio") ||
        !make_directory(audio_directory)) {
        fprintf(stderr, "Could not create local audio directory.\n");
        wm_u8_free(&container);
        free(source);
        free(executable);
        return 1;
    }
    char manifest_path[4096];
    if (!combine_path(manifest_path, argv[3], "", "audio-direct.json")) {
        wm_u8_free(&container);
        free(source);
        free(executable);
        return 1;
    }
    FILE *manifest = fopen(manifest_path, "wb");
    if (!manifest) {
        fprintf(stderr, "Could not create local audio manifest.\n");
        wm_u8_free(&container);
        free(source);
        free(executable);
        return 1;
    }
    bool valid = fputs("{\n", manifest) >= 0;
    size_t exported = 0;
    for (size_t index = 0;
         index < sizeof(direct_sounds) / sizeof(direct_sounds[0]) && valid;
         index++) {
        WmRsarSound sound;
        WmAudioPcm pcm = {0};
        if (!wm_rsar_find_sound(&archive, direct_sounds[index].symbol, &sound) ||
            !wm_rsar_decode_direct_wave(&archive, &sound, &pcm,
                                        error, sizeof(error))) {
            fprintf(stderr, "Could not decode %s: %s\n",
                    direct_sounds[index].symbol, error);
            valid = false;
            break;
        }
        char destination[4096], filename[128];
        int length = snprintf(filename, sizeof(filename), "%s.wav",
                              direct_sounds[index].name);
        if (length <= 0 || length >= (int)sizeof(filename) ||
            !combine_path(destination, argv[3], "audio/", filename) ||
            !wm_audio_wav_write(destination, &pcm, error, sizeof(error))) {
            fprintf(stderr, "Could not export %s: %s\n",
                    direct_sounds[index].symbol, error);
            wm_audio_pcm_free(&pcm);
            valid = false;
            break;
        }
        if (exported > 0) valid = fputs(",\n", manifest) >= 0;
        if (valid) {
            valid = fprintf(manifest,
                            "  \"%s\": {\"sourceSymbol\": \"%s\", "
                            "\"gain\": %.9g, \"loop\": %s, "
                            "\"loopStart\": %.9g, \"loopEnd\": %.9g}",
                            direct_sounds[index].name,
                            direct_sounds[index].symbol,
                            (double)sound.volume / 127.0,
                            pcm.looping ? "true" : "false",
                            (double)pcm.loop_start / pcm.sample_rate,
                            (double)pcm.loop_end / pcm.sample_rate) > 0;
        }
        exported++;
        wm_audio_pcm_free(&pcm);
    }
    if (valid) valid = export_speaker_samples(&container, argv[3],
                                               manifest, &exported);
    if (valid) valid = fputs("\n}\n", manifest) >= 0;
    if (fclose(manifest) != 0) valid = false;
    if (!valid) remove(manifest_path);
    if (!valid) {
        wm_u8_free(&container);
        free(source);
        free(executable);
        return 1;
    }
    printf("Exported %zu original direct-wave and remote-speaker sounds.\n",
           exported);
    valid = export_sequences(&archive, executable, executable_size, argv[3]);
    wm_u8_free(&container);
    free(source);
    free(executable);
    return valid ? 0 : 1;
}
