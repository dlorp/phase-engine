/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Adaptive Signal Filtering for Cortex-M0+
 *
 * Two adaptive filter implementations optimized for the SAM L22 (no FPU,
 * 32KB RAM, 32 MHz Cortex-M0+):
 *
 *   1. Adaptive EMA (AEMA) — replaces fixed-pole EMA in sensor pipeline.
 *      Adjusts smoothing based on prediction error: fast tracking during
 *      signal transitions, heavy smoothing during stable periods.
 *      State: 12 bytes. Computation: ~20 cycles/sample.
 *
 *   2. RLS Filter — Recursive Least-Squares with diagonal inverse
 *      correlation matrix. 2nd-order predictor for multi-tap signal
 *      tracking. O(N) per sample (diagonal P approximation).
 *      State: 32 bytes. Computation: ~60 cycles/sample.
 *
 * All arithmetic is integer (Q15 fixed-point where noted). No floats,
 * no divisions in the hot path (AEMA uses shift-based division).
 *
 * Design reference:
 *   - RLS theory: DSP Book Ch.21 (dsp-book.narod.ru/DSPMW/21.PDF)
 *   - Bare-metal patterns: cpq/bare-metal-programming-guide
 *     (datasheet-first, register-level, no vendor framework overhead)
 *
 * Stability analysis:
 *   - AEMA: stable by construction. Each update is a convex combination
 *     ema_new = (1-alpha)*ema + alpha*input with alpha clamped to
 *     [alpha_min, alpha_max] subset (0, 1) -- the output can never leave
 *     the range spanned by past inputs, regardless of how alpha varies.
 *   - RLS (diagonal-P approximation): NOT proven stable in this fixed-point
 *     form -- treat the P update as heuristic, not guaranteed. The vault
 *     source cited above (signal-processing/2026-07-01-rls-adaptive-filters-dsp-chapter)
 *     lists fixed-point RLS numerical stability as an open question; this
 *     implementation does not resolve it. In particular, P is not proven
 *     to stay positive: `p[i] < 0` is treated as a reachable condition
 *     (see the reset-to-1.0 branch in rls_update()), not a `shouldn't
 *     happen` defensive-only case. The leaky factor γ ∈ (0, 1) and the
 *     explicit clamp on p[i] bound the damage (P can't grow or go negative
 *     unboundedly) but do not constitute a stability proof. This filter
 *     is not currently wired into the sensor pipeline and has no
 *     dedicated test coverage (see tests/test_rls_filter.sh) -- do not
 *     enable it in the live pipeline without adding regression tests
 *     that exercise P over many iterations first.
 */

#ifndef RLS_FILTER_H_
#define RLS_FILTER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef PHASE_ENGINE_ENABLED

/* ============================================================================
 * Adaptive EMA (AEMA)
 *
 * Drop-in replacement for fixed EMA: avg = (avg * 3 + sample) / 4.
 *
 * Where the fixed EMA uses a constant alpha (0.25), the AEMA adjusts alpha
 * based on prediction error magnitude:
 *
 *   error = |input - ema|
 *   alpha = alpha_min + (alpha_max - alpha_min) * error / error_scale
 *   ema   = ema + alpha * (input - ema)
 *
 * When the signal is stable (error ≈ 0): alpha → alpha_min (heavy smoothing)
 * When the signal changes (error large): alpha → alpha_max (fast tracking)
 *
 * The error_scale parameter controls the transition curve. Set it to the
 * expected noise floor — errors below this threshold get minimal adaptation.
 * ========================================================================= */

#define AEMA_ALPHA_Q15     32767  /* 1.0 in Q15 */

/* Default alpha range (Q15):
 * alpha_min = 0.0625 (1/16) → heavy smoothing (16-sample time constant)
 * alpha_max = 0.5           → fast tracking (2-sample time constant)
 * These bracket the fixed EMA's alpha=0.25 */
#define AEMA_ALPHA_MIN_Q15  2048  /* 0.0625 in Q15 */
#define AEMA_ALPHA_MAX_Q15 16384  /* 0.5 in Q15 */

typedef struct {
    int32_t  ema;          /* Current EMA value (actual units, not Q15) */
    int32_t  alpha_min;    /* Minimum adaptation rate (Q15) */
    int32_t  alpha_max;    /* Maximum adaptation rate (Q15) */
    uint16_t error_scale;  /* Error threshold for full adaptation */
    bool     initialized;
} aema_state_t;

/**
 * Initialize AEMA with default parameters.
 * Call once at startup.
 */
void aema_init(aema_state_t *state);

/**
 * Initialize AEMA with custom alpha range and error scale.
 *
 * @param alpha_min_q15  Minimum alpha in Q15 (e.g., 2048 = 0.0625)
 * @param alpha_max_q15  Maximum alpha in Q15 (e.g., 16384 = 0.5)
 * @param error_scale    Error magnitude for full adaptation (e.g., 50)
 */
void aema_init_custom(aema_state_t *state,
                      int32_t alpha_min_q15,
                      int32_t alpha_max_q15,
                      uint16_t error_scale);

/**
 * Update AEMA with new sample. Returns filtered value.
 *
 * @param input  New sensor reading (raw units)
 * @return       Filtered value
 */
int16_t aema_update(aema_state_t *state, int16_t input);

/**
 * Get current filtered value without updating.
 */
int16_t aema_get(const aema_state_t *state);

/**
 * Get current alpha (adaptation rate) for diagnostics.
 * Returns Q15 value (32767 = 1.0).
 */
int32_t aema_get_alpha(const aema_state_t *state, int16_t input);


/* ============================================================================
 * RLS Filter (Recursive Least-Squares, Diagonal-P Variant)
 *
 * 2nd-order adaptive predictor using the RLS algorithm with a diagonal
 * inverse correlation matrix. This is the practical embedded variant of
 * full RLS — O(N) per sample instead of O(N²), while preserving the key
 * benefit: exponential forgetting that adapts to non-stationary signals.
 *
 * Algorithm per sample:
 *   1. Form input vector x = [x[n], x[n-1]]^T
 *   2. Compute gain: k[i] = p[i]*x[i] / (λ + Σ p[j]*x[j]²)
 *   3. Compute output: y = Σ w[i]*x[i]
 *   4. Compute error: e = d - y (desired = raw sample for smoothing)
 *   5. Update weights: w[i] = w[i] + k[i]*e
 *   6. Update P: p[i] = (p[i] - k[i]*p[i]*x[i]) / λ (leaky: * γ)
 *
 * For sensor smoothing, the "desired" signal is the raw sensor reading.
 * The filter learns to predict the next sample from the past N samples.
 * High prediction error → the signal is changing → fast adaptation.
 * Low prediction error → the signal is stable → optimal smoothing.
 *
 * The leaky factor γ (0 < γ < 1) prevents P from growing unboundedly
 * when the signal is very stable (common with sensor noise).
 * ========================================================================= */

#define RLS_ORDER 2

/* Forgetting factor λ = 0.98 in Q15 (32112/32768 ≈ 0.98)
 * Higher → more memory (slower adaptation, smoother output)
 * Lower  → less memory (faster adaptation, noisier output) */
#define RLS_LAMBDA_Q15  32112

/* Leaky factor γ = 0.999 in Q15 (32735/32768 ≈ 0.999)
 * Prevents P wind-up during stable signals */
#define RLS_LEAKY_Q15   32735

/* Regularization δ = 0.01 in Q15 (prevents division by zero) */
#define RLS_DELTA_Q15     328

typedef struct {
    int32_t  p[RLS_ORDER];     /* Diagonal of inverse correlation matrix */
    int32_t  w[RLS_ORDER];     /* Filter weights (Q15) */
    int16_t  x_buf[RLS_ORDER]; /* Input circular buffer */
    int32_t  lambda_q15;       /* Forgetting factor (Q15) */
    int32_t  leaky_q15;        /* Leaky factor for P (Q15) */
    int32_t  delta_q15;        /* Regularization (Q15) */
    uint8_t  idx;              /* Current position in circular buffer */
    bool     initialized;
} rls_state_t;

/**
 * Initialize RLS filter with default parameters.
 * λ=0.98, γ=0.999, δ=0.01, P₀=1.0
 */
void rls_init(rls_state_t *state);

/**
 * Initialize RLS filter with custom parameters.
 *
 * @param lambda_q15  Forgetting factor in Q15 (e.g., 32112 = 0.98)
 * @param leaky_q15   Leaky factor in Q15 (e.g., 32735 = 0.999)
 * @param delta_q15   Regularization in Q15 (e.g., 328 = 0.01)
 */
void rls_init_custom(rls_state_t *state,
                     int32_t lambda_q15,
                     int32_t leaky_q15,
                     int32_t delta_q15);

/**
 * Update RLS filter with new sample. Returns filtered value.
 *
 * @param input  New sensor reading (raw units)
 * @return       Filtered (predicted) value
 */
int16_t rls_update(rls_state_t *state, int16_t input);

/**
 * Get current filtered value without updating.
 */
int16_t rls_get(const rls_state_t *state);

/**
 * Get current prediction error magnitude for diagnostics.
 * High error = signal changing rapidly. Low error = stable signal.
 */
int16_t rls_get_error(const rls_state_t *state, int16_t input);

#endif /* PHASE_ENGINE_ENABLED */
#endif /* RLS_FILTER_H_ */
