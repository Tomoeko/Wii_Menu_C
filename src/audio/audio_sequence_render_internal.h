#ifndef WII_MENU_AUDIO_SEQUENCE_RENDER_INTERNAL_H
#define WII_MENU_AUDIO_SEQUENCE_RENDER_INTERNAL_H

#include "audio_sequence_driver_internal.h"
#include "wii_menu/audio/audio_sequence.h"

enum {
    WM_SEQUENCE_BLOCK = 96,
    WM_SEQUENCE_MAX_VOICES = 128,
    WM_SEQUENCE_MAX_WAVES = 256
};

typedef struct SequenceWave {
    uint32_t index;
    WmAudioPcm pcm;
} SequenceWave;

typedef struct PreparedNote {
    WmRsarInstrument instrument;
    uint16_t wave_slot;
} PreparedNote;

typedef struct SequenceTrack {
    uint8_t volume;
    uint8_t volume2;
    uint8_t pan;
    uint8_t main_send;
    uint8_t aux_a;
    uint8_t aux_b;
    uint8_t aux_c;
    int16_t envelope[4];
    size_t event_index;
    uint32_t tick;
    bool waiting_for_end;
} SequenceTrack;

typedef struct SequenceVoice {
    const WmRsarInstrument *instrument;
    const WmAudioPcm *wave;
    uint32_t release_tick;
    double position;
    double speed;
    float initial_gain;
    float previous_gain;
    float envelope_level;
    uint8_t envelope[4];
    uint8_t track;
    uint8_t envelope_state;
    bool has_previous_gain;
} SequenceVoice;

typedef struct SequencePlayer {
    const WmSequenceTimeline *timeline;
    const PreparedNote *prepared;
    const SequenceWave *waves;
    const SequenceTables *tables;
    SequenceTrack tracks[16];
    SequenceVoice voices[WM_SEQUENCE_MAX_VOICES];
    size_t voice_count;
    uint32_t pending_until[16];
    uint32_t song_tick;
    uint32_t elapsed_ticks;
    uint32_t tempo_counter;
    uint32_t tempo;
    uint32_t block_position;
    uint8_t main_volume;
    size_t event_index;
    size_t loop_event_index;
    size_t voice_loop_event_index;
    uint32_t voice_loop_start_frame;
    bool voice_loop_started;
    float gain;
    float block_left[WM_SEQUENCE_BLOCK];
    float block_right[WM_SEQUENCE_BLOCK];
    float aux_left[WM_SEQUENCE_BLOCK];
    float aux_right[WM_SEQUENCE_BLOCK];
} SequencePlayer;

typedef struct DelayLine {
    float *samples;
    uint32_t length;
    uint32_t position;
} DelayLine;

typedef struct ReverbChannel {
    DelayLine comb[3];
    DelayLine all_pass[2];
    DelayLine final;
    float last;
} ReverbChannel;

typedef struct SequenceReverb {
    ReverbChannel channels[2];
    float comb_gain[3];
    float coloration;
    float low_pass;
    float output_gain;
    float aux_return[2][WM_SEQUENCE_BLOCK * 2];
    size_t aux_return_position;
    bool enabled;
} SequenceReverb;

/* The scheduler owns event order and voice lifetime. This module owns
 * per-sample voice output and the optional auxiliary return delay network. */
void wm_sequence_voice_render(SequencePlayer *player, SequenceVoice *voice);
bool wm_sequence_reverb_initialize(SequenceReverb *reverb, const SequenceTables *tables,
                                   bool enabled);
void wm_sequence_reverb_free(SequenceReverb *reverb);
void wm_sequence_reverb_apply(SequenceReverb *reverb, SequencePlayer *player);

#endif
