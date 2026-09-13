/* MIT License
 *
 * Copyright (c) 2026 Diego Perez
 * Phase Engine: YM-2149 PSG Tone Patterns
 *
 * Each pattern is a static const int8_t[] using the watch_buzzer_play_sequence()
 * format: pairs of [note, duration_ticks] terminated by 0.
 *
 * Design principles:
 *   - Short (< 1.5 seconds) to minimize buzzer-on time and battery drain
 *   - Distinct character per score (ascending/descending/staccato/drone)
 *   - PSG-inspired: arpeggios, not melodies; register patterns, not songs
 *   - All integer math, no FPU, Cortex M0+ compatible
 */

#include "psg_tones.h"

#ifdef PHASE_ENGINE_ENABLED

#include "watch_tcc.h"

/* ─────────────────────────────────────────────────────────────
 * Sol (SO) — Circadian Alignment
 *
 * Major arpeggio cascade: C5→E5→G5, repeating with octave climb.
 * The three-note motif repeats twice then resolves upward.
 * Inspired by PSG Channel A doing a rising major triad.
 *
 * Timing: 4+4+4 + 4+4+4 + 4+4+4 + 6+6+6+8 = 72 ticks ≈ 1.125s
 * ───────────────────────────────────────────────────────────── */
const int8_t psg_tone_sol[] = {
    BUZZER_NOTE_C5, 4,                 /* 523 Hz — root, quick */
    BUZZER_NOTE_E5, 4,                 /* 659 Hz — major third */
    BUZZER_NOTE_G5, 4,                 /* 784 Hz — fifth */
    BUZZER_NOTE_C5, 4,                 /* repeat motif */
    BUZZER_NOTE_E5, 4,
    BUZZER_NOTE_G5, 4,
    BUZZER_NOTE_E5, 4,                 /* inversion — third on bottom */
    BUZZER_NOTE_G5, 4,
    BUZZER_NOTE_C6, 4,                 /* octave reach */
    BUZZER_NOTE_G5, 6,                 /* resolution — slower */
    BUZZER_NOTE_C6, 6,
    BUZZER_NOTE_E6, 6,
    BUZZER_NOTE_C6, 8,                 /* final sustain */
    0                                  /* terminator */
};

/* ─────────────────────────────────────────────────────────────
 * Dew (DE) — Stillness Quality
 *
 * Descending minor arpeggio with breathing rests.
 * Overdamped decay — each note gets quieter feeling via rests.
 * Inspired by PSG Channel B doing a falling minor triad.
 *
 * Timing: 8+3+8+3+8+3 + 6+3+6+3 + 4+3 = 58 ticks ≈ 0.906s
 * ───────────────────────────────────────────────────────────── */
const int8_t psg_tone_dew[] = {
    BUZZER_NOTE_E5, 8,                 /* 659 Hz — sustained entry */
    BUZZER_NOTE_REST, 3,               /* breathing space */
    BUZZER_NOTE_C5, 8,                 /* 523 Hz — step down */
    BUZZER_NOTE_REST, 3,
    BUZZER_NOTE_G4, 8,                 /* 392 Hz — fifth below */
    BUZZER_NOTE_REST, 3,
    BUZZER_NOTE_E4, 6,                 /* 330 Hz — lower, shorter */
    BUZZER_NOTE_REST, 3,
    BUZZER_NOTE_C4, 6,                 /* 262 Hz — bottom of range */
    BUZZER_NOTE_REST, 3,
    BUZZER_NOTE_E4, 4,                 /* slight rise — settling */
    BUZZER_NOTE_REST, 3,               /* final rest */
    0                                  /* terminator */
};

/* ─────────────────────────────────────────────────────────────
 * Sap (SA) — Movement Density
 *
 * Staccato octave jumps with rhythmic silence.
 * High-low-high-low alternation — kinetic energy bouncing
 * between registers. Rests create the stuttering cadence.
 * Inspired by PSG Channel C doing rapid octave switching.
 *
 * Timing: 2+2+2+2 + 4+2+2+2+2+4 + 2+2+2+2+4+2+2 = 48 ticks ≈ 0.75s
 * ───────────────────────────────────────────────────────────── */
const int8_t psg_tone_sap[] = {
    BUZZER_NOTE_G5, 2,                 /* 784 Hz — high blip */
    BUZZER_NOTE_REST, 2,               /* snap silence */
    BUZZER_NOTE_G4, 2,                 /* 392 Hz — octave drop */
    BUZZER_NOTE_REST, 2,               /* snap silence */
    BUZZER_NOTE_A5, 4,                 /* 880 Hz — higher, longer */
    BUZZER_NOTE_G4, 2,                 /* drop again */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_B5, 2,                 /* 988 Hz — peak */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_G4, 4,                 /* anchor — longer low */
    BUZZER_NOTE_G5, 2,                 /* quick bounce back up */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_E5, 2,                 /* 659 Hz — landing */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_G4, 4,                 /* final low anchor */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_G5, 2,                 /* parting blip */
    0                                  /* terminator */
};

/* ─────────────────────────────────────────────────────────────
 * Hum (HU) — Environmental Harmony
 *
 * Slow wide-interval arpeggio — drone-like spacing.
 * Each note is long, gaps are minimal. The wide intervals
 * (minor 3rd, perfect 4th, major 2nd) create an open,
 * spacious feel. No sharp attacks.
 * Inspired by PSG doing slow register sweeps across channels.
 *
 * Timing: 10+2+12+2+14+2+12+2+10+2+12+2+2 = 82 ticks ≈ 1.28s
 * ───────────────────────────────────────────────────────────── */
const int8_t psg_tone_hum[] = {
    BUZZER_NOTE_C4, 10,                /* 262 Hz — deep foundation */
    BUZZER_NOTE_REST, 2,               /* minimal gap */
    BUZZER_NOTE_E4, 12,                /* 330 Hz — minor 3rd, sustained */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_A4, 14,                /* 440 Hz — perfect 4th, longest */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_D5, 12,                /* 587 Hz — major 2nd up */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_G4, 10,                /* 392 Hz — step back */
    BUZZER_NOTE_REST, 2,
    BUZZER_NOTE_C5, 12,                /* 523 Hz — octave from root */
    BUZZER_NOTE_REST, 2,               /* trailing silence */
    0                                  /* terminator */
};

/* ─────────────────────────────────────────────────────────────
 * Public API
 * ───────────────────────────────────────────────────────────── */

static const int8_t *const psg_tone_table[4] = {
    psg_tone_sol,
    psg_tone_dew,
    psg_tone_sap,
    psg_tone_hum
};

const int8_t *psg_tone_for_score(uint8_t score_index) {
    if (score_index >= 4) return NULL;
    return psg_tone_table[score_index];
}

void psg_tone_play(uint8_t score_index, void (*callback)(void)) {
    const int8_t *pattern = psg_tone_for_score(score_index);
    if (pattern == NULL) return;
    watch_buzzer_play_sequence((int8_t *)pattern, callback);
}

void psg_tone_play_pattern(const int8_t *pattern, void (*callback)(void)) {
    if (pattern == NULL) return;
    watch_buzzer_play_sequence((int8_t *)pattern, callback);
}

#endif /* PHASE_ENGINE_ENABLED */
