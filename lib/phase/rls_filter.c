/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Adaptive Signal Filtering Implementation
 *
 * Fixed-point adaptive filters for Cortex-M0+ (SAM L22).
 * All math is integer — no FPU required.
 *
 * Architecture notes (bare-metal, datasheet-first):
 *   - Cortex-M0+ has no hardware divide. We use shift-based division
 *     for the AEMA and 64-bit intermediates for RLS division.
 *   - The Cortex-M0+ UMULL instruction gives 32×32→64-bit multiply
 *     in 32 cycles. We use this for Q15×Q15 products.
 *   - All state fits in registers or L1 SRAM (32KB on SAM L22).
 *     The AEMA state (12 bytes) fits entirely in registers.
 *   - No dynamic allocation. No function pointers. No indirect calls.
 *     The compiler can inline everything for zero call overhead.
 *
 * References:
 *   - RLS theory: DSP Book Ch.21 (dsp-book.narod.ru)
 *   - Fixed-point DSP: Labrosse/Numerical Recipes patterns
 *   - Cortex-M0+ ISA: ARM Architecture Reference Manual (ARMv6-M)
 */

#ifdef PHASE_ENGINE_ENABLED

#include "rls_filter.h"
#include <string.h>

/* ============================================================================
 * Adaptive EMA Implementation
 *
 * The AEMA is the primary sensor filter. It replaces the fixed-pole EMA
 * (alpha = 0.25, pole at 0.75) with an adaptive version that adjusts
 * its smoothing based on prediction error.
 *
 * Integer implementation:
 *   alpha = alpha_min + ((alpha_max - alpha_min) * abs_error) / error_scale
 *   ema   = ema + ((alpha * (input - ema)) >> 15)
 *
 * The shift-based division (>> 15) replaces the Q15 multiply.
 * We clamp alpha to [alpha_min, alpha_max] to ensure stability.
 * ========================================================================= */

void aema_init(aema_state_t *state) {
    aema_init_custom(state, AEMA_ALPHA_MIN_Q15, AEMA_ALPHA_MAX_Q15, 100);
}

void aema_init_custom(aema_state_t *state,
                      int32_t alpha_min_q15,
                      int32_t alpha_max_q15,
                      uint16_t error_scale) {
    memset(state, 0, sizeof(aema_state_t));
    state->alpha_min = alpha_min_q15;
    state->alpha_max = alpha_max_q15;
    state->error_scale = error_scale > 0 ? error_scale : 100;
    state->initialized = true;
}

int16_t aema_update(aema_state_t *state, int16_t input) {
    if (!state || !state->initialized) {
        return input;
    }

    /* First sample: initialize EMA directly (no convergence from zero) */
    if (state->ema == 0) {
        state->ema = (int32_t)input;
        return input;
    }

    /* Prediction error */
    int32_t error = (int32_t)input - state->ema;

    /* Absolute error (unsigned for alpha computation) */
    uint32_t abs_error = (error < 0) ? (uint32_t)(-error) : (uint32_t)error;

    /* Adaptive alpha:
     * alpha = alpha_min + (alpha_max - alpha_min) * abs_error / error_scale
     *
     * Clamped to [alpha_min, alpha_max] for stability.
     * The multiplication is done in 32-bit to avoid overflow:
     *   (alpha_range * abs_error) max ≈ 14336 * 1000 = 14,336,000 (fits int32)
     */
    int32_t alpha_range = state->alpha_max - state->alpha_min;
    int32_t alpha;

    if (abs_error >= state->error_scale) {
        /* Error exceeds scale → use maximum alpha (fast tracking) */
        alpha = state->alpha_max;
    } else {
        /* Linear interpolation between alpha_min and alpha_max */
        int32_t alpha_delta = (alpha_range * (int32_t)abs_error)
                              / (int32_t)state->error_scale;
        alpha = state->alpha_min + alpha_delta;
    }

    /* EMA update: ema = ema + (alpha * error) / 32768
     *
     * alpha is Q15 (0..32767), error is in sensor units.
     * Product: alpha * error fits in 32 bits for typical sensor ranges
     * (error ≤ 1000, alpha ≤ 32767 → product ≤ 32,767,000 < 2^31)
     *
     * The shift-right-by-15 converts from Q15 back to sensor units.
     * Arithmetic right shift preserves sign for negative corrections.
     */
    int32_t correction = (alpha * error) >> 15;
    state->ema += correction;

    return (int16_t)state->ema;
}

int16_t aema_get(const aema_state_t *state) {
    return state ? (int16_t)state->ema : 0;
}

int32_t aema_get_alpha(const aema_state_t *state, int16_t input) {
    if (!state || !state->initialized) {
        return 0;
    }

    int32_t error = (int32_t)input - state->ema;
    uint32_t abs_error = (error < 0) ? (uint32_t)(-error) : (uint32_t)error;
    int32_t alpha_range = state->alpha_max - state->alpha_min;

    if (abs_error >= state->error_scale) {
        return state->alpha_max;
    }
    return state->alpha_min + (alpha_range * (int32_t)abs_error)
                              / (int32_t)state->error_scale;
}


/* ============================================================================
 * RLS Filter Implementation (Diagonal-P Variant)
 *
 * The RLS filter provides optimal prediction in the least-squares sense.
 * The diagonal-P approximation reduces the full NxN matrix update to N
 * independent scalar updates — O(N) instead of O(N²) per sample.
 *
 * For N=2 (2nd-order predictor):
 *   - Predicts current sample from past 2 samples
 *   - Learns the signal's local linear trend
 *   - Adapts forgetting factor controls memory depth
 *
 * The "leaky" variant multiplies P by γ < 1 each step to prevent
 * wind-up during stationary signals (common with sensor noise).
 *
 * Fixed-point implementation notes:
 *   - P values can grow large (they're inverse correlation, not correlation)
 *   - We use int32_t for P and w to handle the dynamic range
 *   - The gain computation uses 64-bit intermediates to avoid overflow:
 *     k[i] = (p[i] * x[i]) / (λ * delta + Σ p[j] * x[j]²)
 *   - The denominator is accumulated in int64_t
 *   - Division uses int64_t / int32_t (Cortex-M0+ has no hardware divide,
 *     but the compiler generates a software routine that's ~100 cycles)
 * ========================================================================= */

void rls_init(rls_state_t *state) {
    rls_init_custom(state, RLS_LAMBDA_Q15, RLS_LEAKY_Q15, RLS_DELTA_Q15);
}

void rls_init_custom(rls_state_t *state,
                     int32_t lambda_q15,
                     int32_t leaky_q15,
                     int32_t delta_q15) {
    memset(state, 0, sizeof(rls_state_t));

    /* Initialize P to identity * initial_gain
     * P₀ = δ⁻¹ * I (large initial value ensures fast initial convergence)
     * We use 1.0 (32768 in Q15) as the initial P diagonal */
    for (uint8_t i = 0; i < RLS_ORDER; i++) {
        state->p[i] = 32768;  /* 1.0 in Q15 */
        state->w[i] = 0;      /* Zero initial weights */
    }

    state->lambda_q15 = lambda_q15;
    state->leaky_q15 = leaky_q15;
    state->delta_q15 = delta_q15;
    state->idx = 0;
    state->initialized = true;
}

int16_t rls_update(rls_state_t *state, int16_t input) {
    if (!state || !state->initialized) {
        return input;
    }

    /* ------------------------------------------------------------------
     * Step 1: Insert new sample into circular buffer
     *
     * The buffer stores the most recent RLS_ORDER samples.
     * x_buf[idx] is the oldest sample (will be overwritten).
     * After update, x_buf[idx] holds the newest sample.
     * ------------------------------------------------------------------ */
    state->x_buf[state->idx] = input;

    /* ------------------------------------------------------------------
     * Step 2: Compute input vector and dot products
     *
     * We iterate through the circular buffer to form the input vector
     * x = [x[n], x[n-1], ... , x[n-N+1]].
     *
     * For N=2: x = [x_buf[idx], x_buf[(idx+1) % 2]]
     *
     * We compute:
     *   - output y = Σ w[i] * x[i]  (filter output)
     *   - denom    = λ*δ + Σ p[i] * x[i]²  (gain denominator)
     * ------------------------------------------------------------------ */
    int32_t output = 0;
    int64_t denom = (int64_t)state->lambda_q15 * (int64_t)state->delta_q15;

    /* Temporary arrays for input values (needed for gain computation) */
    int16_t x[RLS_ORDER];
    int32_t px2[RLS_ORDER];  /* p[i] * x[i]² (for gain numerator) */

    for (uint8_t i = 0; i < RLS_ORDER; i++) {
        uint8_t buf_idx = (state->idx + i) % RLS_ORDER;
        x[i] = state->x_buf[buf_idx];

        /* Filter output: w[i] * x[i] (Q15 * Q15 → Q30, shift to Q15) */
        output += ((int32_t)state->w[i] * (int32_t)x[i]) >> 15;

        /* p[i] * x[i]² for gain denominator
         * x[i]² max ≈ 32767² = 1,073,676,289 (fits int32_t if p[i] is small)
         * But p[i] * x[i]² can overflow int32, so we use int64 */
        int32_t x2 = (int32_t)x[i] * (int32_t)x[i];
        px2[i] = (int32_t)(((int64_t)state->p[i] * (int64_t)x2) >> 15);
        denom += (int64_t)px2[i];
    }

    /* ------------------------------------------------------------------
     * Step 3: Compute error
     *
     * For sensor smoothing, the desired signal IS the raw input.
     * The filter learns to predict the current sample from past samples.
     * High error = signal changing → fast adaptation.
     * Low error = signal stable → optimal smoothing.
     * ------------------------------------------------------------------ */
    int32_t error = (int32_t)input - output;

    /* ------------------------------------------------------------------
     * Step 4: Compute gain vector and update weights
     *
     * k[i] = p[i] * x[i] / denom
     * w[i] = w[i] + k[i] * error
     *
     * The gain is computed in Q15. The denominator is in Q30 (from the
     * p*x² products), so we need to adjust the scaling.
     *
     * We use 64-bit division to avoid overflow in the gain computation.
     * ------------------------------------------------------------------ */
    for (uint8_t i = 0; i < RLS_ORDER; i++) {
        /* Gain numerator: p[i] * x[i] (Q15 * Q15 → Q30) */
        int64_t k_num = ((int64_t)state->p[i] * (int64_t)x[i]);

        /* Gain: k = k_num / denom (both in Q30, result in Q15)
         * Use 64-bit division for precision */
        int32_t k_i;
        if (denom != 0) {
            k_i = (int32_t)((k_num << 15) / denom);
        } else {
            k_i = 0;
        }

        /* Weight update: w[i] += k[i] * error
         * k[i] is Q15, error is in sensor units.
         * Product: k[i] * error / 32768 (Q15 → units) */
        int32_t w_correction = (k_i * error) >> 15;
        state->w[i] += w_correction;

        /* ------------------------------------------------------------------
         * Step 5: Update P (diagonal, with leaky factor)
         *
         * p[i] = (p[i] - k[i]² * x[i]²) / λ * γ
         *
         * The leaky factor γ < 1 prevents P from growing unboundedly
         * during stationary signals (sensor noise floor).
         *
         * k[i]² is Q30, x[i]² is in units². The product needs careful
         * scaling to avoid overflow.
         * ------------------------------------------------------------------ */

        /* k[i]² * x[i]² — scale to prevent overflow */
        int64_t k2 = (int64_t)k_i * (int64_t)k_i;
        int64_t x2 = (int64_t)x[i] * (int64_t)x[i];
        int64_t p_correction = (k2 * x2) >> 30;  /* Scale down */

        /* Update P with leaky factor: p = (p - correction) * γ / λ */
        int64_t new_p = (int64_t)state->p[i] - p_correction;

        /* Apply leaky factor: p *= γ (Q15 multiply) */
        new_p = (new_p * (int64_t)state->leaky_q15) >> 15;

        /* Apply forgetting factor: p /= λ (Q15 divide)
         * This is the expensive part — but only N divisions per sample */
        if (state->lambda_q15 > 0) {
            new_p = (new_p << 15) / (int64_t)state->lambda_q15;
        }

        /* Clamp P to prevent overflow (max 2^20 in Q15 ≈ 32x initial) */
        if (new_p > 33554432) {  /* 2^25 — generous bound */
            new_p = 33554432;
        }
        if (new_p < 0) {
            new_p = 32768;  /* Reset to 1.0 if negative (shouldn't happen) */
        }

        state->p[i] = (int32_t)new_p;
    }

    /* ------------------------------------------------------------------
     * Step 6: Advance circular buffer index
     * ------------------------------------------------------------------ */
    state->idx = (state->idx + 1) % RLS_ORDER;

    /* Return filtered output (clamp to int16 range) */
    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    return (int16_t)output;
}

int16_t rls_get(const rls_state_t *state) {
    if (!state || !state->initialized) {
        return 0;
    }

    /* Compute current output from weights and input buffer */
    int32_t output = 0;
    for (uint8_t i = 0; i < RLS_ORDER; i++) {
        uint8_t buf_idx = (state->idx + i) % RLS_ORDER;
        output += ((int32_t)state->w[i] * (int32_t)state->x_buf[buf_idx]) >> 15;
    }

    if (output > 32767) output = 32767;
    if (output < -32768) output = -32768;
    return (int16_t)output;
}

int16_t rls_get_error(const rls_state_t *state, int16_t input) {
    if (!state || !state->initialized) {
        return 0;
    }

    int16_t predicted = rls_get(state);
    int32_t error = (int32_t)input - (int32_t)predicted;

    if (error > 32767) error = 32767;
    if (error < -32768) error = -32768;
    return (int16_t)error;
}

#endif /* PHASE_ENGINE_ENABLED */
