/* MIT License
 *
 * Copyright (c) 2026 Diego Perez
 * Phase Engine: YM-2149 PSG Tone Patterns
 *
 * Distinctive arpeggio sequences for each score notification.
 * Inspired by the YM-2149 / AY-3-8910 programmable sound generator —
 * three square-wave channels, duty-cycle modulation, tight register
 * constraints. Each pattern is ~0.5-1.5 seconds, non-blocking, and
 * uses the watch_buzzer_play_sequence() API.
 *
 * Duration unit: TC0 ticks at 64 Hz (1 tick ≈ 15.625 ms).
 * Format: int8_t pairs of [note, duration], terminated by 0.
 * Negative note values trigger repeat markers (see watch_tcc.h).
 */

#ifndef PSG_TONES_H_
#define PSG_TONES_H_

#include <stdint.h>

#ifdef PHASE_ENGINE_ENABLED

#include "watch_tcc.h"

/* Pattern duration reference (ticks at 64 Hz):
 *   2 ticks  =  ~31 ms   (staccato blip)
 *   4 ticks  =  ~62 ms   (quick note)
 *   6 ticks  =  ~94 ms   (normal note)
 *   8 ticks  = ~125 ms   (sustained note)
 *  12 ticks  = ~188 ms   (long note)
 *  16 ticks  = ~250 ms   (half-second feel)
 */

/*
 * Sol (SO) — Circadian Alignment
 *
 * Bright major arpeggio ascending through two octaves.
 * Three-note repeat creates a rising cascade — like dawn breaking.
 * Character: optimistic, solar, energizing.
 * Duration: ~1.1 seconds (72 ticks).
 */
extern const int8_t psg_tone_sol[];

/*
 * Dew (DE) — Stillness Quality
 *
 * Descending minor arpeggio with soft rests.
 * Overdamped feel — slow, smooth, minimal energy.
 * Character: quiet, introspective, settling.
 * Duration: ~0.9 seconds (58 ticks).
 */
extern const int8_t psg_tone_dew[];

/*
 * Sap (SA) — Movement Density
 *
 * Staccato octave jumps with rhythmic rests.
 * Stuttering pattern — high-low-high-low with silence gaps.
 * Character: kinetic, punchy, alive.
 * Duration: ~0.75 seconds (48 ticks).
 */
extern const int8_t psg_tone_sap[];

/*
 * Hum (HU) — Environmental Harmony
 *
 * Slow drone arpeggio — wide intervals, even rhythm.
 * Sine-drift feel: no sharp attacks, sustained tones.
 * Character: ambient, spacious, grounded.
 * Duration: ~1.3 seconds (84 ticks).
 */
extern const int8_t psg_tone_hum[];

/**
 * Get the PSG tone pattern for a given score index.
 *
 * @param score_index 0=Sol, 1=Dew, 2=Sap, 3=Hum
 * @return Pointer to the tone sequence, or NULL if invalid index
 */
const int8_t *psg_tone_for_score(uint8_t score_index);

/**
 * Play the PSG tone for a given score.
 * Non-blocking — returns immediately, buzzer plays in background.
 *
 * @param score_index 0=Sol, 1=Dew, 2=Sap, 3=Hum
 * @param callback Optional callback when tone finishes (may be NULL)
 */
void psg_tone_play(uint8_t score_index, void (*callback)(void));

/**
 * Play a specific tone pattern.
 * Non-blocking — returns immediately, buzzer plays in background.
 *
 * @param pattern Pointer to tone sequence (psg_tone_sol, etc.)
 * @param callback Optional callback when tone finishes (may be NULL)
 */
void psg_tone_play_pattern(const int8_t *pattern, void (*callback)(void));

#endif // PHASE_ENGINE_ENABLED

#endif // PSG_TONES_H_
