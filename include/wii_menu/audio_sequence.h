#ifndef WII_MENU_AUDIO_SEQUENCE_H
#define WII_MENU_AUDIO_SEQUENCE_H

#include "wii_menu/resource_rsar.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum WmSequenceEventKind {
    WM_SEQUENCE_NOTE,
    WM_SEQUENCE_TEMPO,
    WM_SEQUENCE_VOLUME,
    WM_SEQUENCE_VOLUME2,
    WM_SEQUENCE_MAIN_VOLUME,
    WM_SEQUENCE_PAN,
    WM_SEQUENCE_MAIN_SEND,
    WM_SEQUENCE_AUX_A,
    WM_SEQUENCE_AUX_B,
    WM_SEQUENCE_AUX_C,
    WM_SEQUENCE_ATTACK,
    WM_SEQUENCE_DECAY,
    WM_SEQUENCE_SUSTAIN,
    WM_SEQUENCE_RELEASE
} WmSequenceEventKind;

typedef struct WmSequenceEvent {
    uint32_t tick;
    uint32_t order;
    uint32_t length;
    uint32_t program;
    uint16_t value;
    uint8_t track;
    uint8_t key;
    uint8_t velocity;
    uint8_t kind;
    bool wait_for_end;
} WmSequenceEvent;

typedef struct WmSequenceTimeline {
    WmSequenceEvent *events;
    size_t count;
    uint32_t loop_start_tick;
    uint32_t loop_end_tick;
    uint32_t loop_start_event_order;
    bool looping;
    bool voice_wait_loop;
    bool has_wait_for_end;
} WmSequenceTimeline;

/* Parse only source commands used by the HTML sequence renderer. Unsupported
 * commands fail visibly instead of silently replacing a sound. */
bool wm_sequence_parse(const WmRsarSequence *sequence,
                       WmSequenceTimeline *timeline,
                       char *error, size_t error_capacity);
void wm_sequence_timeline_free(WmSequenceTimeline *timeline);

/* Synthesize from a caller-owned RSAR and matching System Menu executable.
 * The executable supplies its attack, sustain, volume, and pan tables. Output
 * is 32 kHz stereo PCM; looping output contains one introduction and one full
 * loop, with loop_start/end marking the repeatable section. */
bool wm_sequence_render(const WmRsar *archive, const WmRsarSound *sound,
                        const uint8_t *system_menu_dol, size_t dol_size,
                        WmAudioPcm *output, char *error,
                        size_t error_capacity);

#endif
