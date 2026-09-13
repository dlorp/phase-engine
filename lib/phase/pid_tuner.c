/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Data-Driven PID Auto-Tuning Implementation
 *
 * Fixed-point FOPDT identification + PID tuning for Cortex-M0+ (SAM L22).
 * All math is integer; int64 intermediates only. No FPU.
 *
 * Architecture notes (bare-metal, datasheet-first):
 *   - Cortex-M0+ has no hardware divide. Division appears only in the
 *     tuner (off the hot path, one step test per hours/days) and in the
 *     derivative term of the PID (per update). Software divide is ~100
 *     cycles; acceptable at control-loop rates.
 *   - The PID hot path uses shifts (>> 8) for the Q8 gains, avoiding
 *     division entirely except the dt-normalized derivative.
 *   - No dynamic allocation, no function pointers, no indirect calls.
 *
 * References:
 *   - IMC: Rivera, Morari, Skogestad 1986
 *   - ZN open-loop: Ziegler & Nichols 1942
 *   - Fixed-point DSP: Labrosse/Numerical Recipes patterns
 *   - Cortex-M0+ ISA: ARM Architecture Reference Manual (ARMv6-M)
 */

#ifdef PHASE_ENGINE_ENABLED

#include "pid_tuner.h"
#include <string.h>

/* IMC aggressiveness: lambda = IMC_LAMBDA_T_FRAC/1000 * T.
 * 250 = 0.25*T: aggressive-but-robust, ~10-20% overshoot for FOPDT. */
#define PID_TUNER_IMC_LAMBDA_FRAC 250

/* ---- internal helpers -------------------------------------------------- */

static int32_t clamp_q8(int32_t v) {
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return v;
}

/* Round-to-nearest (ties away from zero) integer division. `den` must be
 * > 0. Plain `/` truncates toward zero, which for ki_q8 = kp_q8/ti_ms can
 * knock a small-but-nonzero true ratio down to exactly 0 (killing integral
 * action silently) or lose ~1 LSB worth of Q8 precision on every tuning
 * run — this recovers that LSB. */
static int32_t round_div_i64(int64_t num, int64_t den) {
    if (den <= 0) return 0;
    if (num >= 0) {
        return (int32_t)((num + den / 2) / den);
    }
    return -(int32_t)((-num + den / 2) / den);
}

/* Find the time (ms) at which the response first reaches `frac_permille`
 * (0-1000) of the total response, by linear interpolation between samples.
 * Returns 0 if the threshold is never reached. */
static uint32_t find_crossing_time(const pid_tuner_state_t *state,
                                   int16_t y_baseline,
                                   int32_t dy_abs,
                                   uint16_t frac_permille) {
    if (dy_abs <= 0 || state->sample_count < 2) {
        return 0;
    }

    int64_t target = (int64_t)dy_abs * (int64_t)frac_permille / 1000;

    for (uint16_t i = 1; i < state->sample_count; i++) {
        int64_t prev_mag = (int64_t)state->samples[i - 1].y - (int64_t)y_baseline;
        int64_t curr_mag = (int64_t)state->samples[i].y - (int64_t)y_baseline;
        if (prev_mag < 0) prev_mag = -prev_mag;
        if (curr_mag < 0) curr_mag = -curr_mag;

        if (prev_mag < target && curr_mag >= target) {
            /* interpolate: t = t_prev + (target - prev_mag)/(curr-prev) * dt */
            int64_t span = curr_mag - prev_mag;
            if (span <= 0) continue;
            uint32_t dt = state->samples[i].t_ms - state->samples[i - 1].t_ms;
            int64_t frac = (target - prev_mag) * dt / span;
            return state->samples[i - 1].t_ms + (uint32_t)frac;
        }
    }
    return 0;
}

/* ---- tuner API --------------------------------------------------------- */

void pid_tuner_init(pid_tuner_state_t *state) {
    if (!state) return;
    memset(state, 0, sizeof(pid_tuner_state_t));
    state->initialized = true;
}

void pid_tuner_begin(pid_tuner_state_t *state,
                     int32_t step_amplitude,
                     int16_t y_baseline,
                     uint32_t step_start_ms) {
    if (!state) return;
    if (!state->initialized) pid_tuner_init(state);
    state->sample_count = 0;
    state->step_amplitude = step_amplitude;
    state->y_baseline = y_baseline;
    state->step_start_ms = step_start_ms;
}

void pid_tuner_add_sample(pid_tuner_state_t *state, uint32_t t_ms, int16_t y) {
    if (!state || !state->initialized) return;

    if (state->sample_count < PID_TUNER_MAX_SAMPLES) {
        state->samples[state->sample_count].t_ms = t_ms;
        state->samples[state->sample_count].y = y;
        state->sample_count++;
    } else {
        /* Ring buffer full: shift down, keep most recent PID_TUNER_MAX_SAMPLES */
        for (uint16_t i = 1; i < PID_TUNER_MAX_SAMPLES; i++) {
            state->samples[i - 1] = state->samples[i];
        }
        state->samples[PID_TUNER_MAX_SAMPLES - 1].t_ms = t_ms;
        state->samples[PID_TUNER_MAX_SAMPLES - 1].y = y;
    }
}

/* Map a fitted FOPDT model to IMC-tuned Q8 PID gains.
 * IMC formulas for K*e^(-Ls)/(Ts+1):
 *   Kp = (T + L/2) / (K * (lambda + L/2))
 *   Ti = T + L/2
 *   Td = T*L / (2T + L)
 * Returns gains via out; out->status set to PID_TUNE_OK. */
static void imc_tune(const int32_t process_gain_q15,
                     uint32_t T_ms,
                     uint32_t L_ms,
                     pid_tune_result_t *out) {
    uint32_t lambda_ms = (uint32_t)((int64_t)T_ms * PID_TUNER_IMC_LAMBDA_FRAC / 1000);
    if (lambda_ms == 0) lambda_ms = 1;

    int64_t half_L = (int64_t)L_ms / 2;
    int64_t num = (int64_t)T_ms + half_L;              /* T + L/2 */
    int64_t den = (int64_t)lambda_ms + half_L;          /* lambda + L/2 */

    int32_t kp_q8 = 0;
    if (process_gain_q15 != 0 && den != 0) {
        /* kp_q8 = 256 * Kp = 256 * num / (K * den)
         *       = 256 * num * Q15 / (K_q15 * den) */
        kp_q8 = (int32_t)((int64_t)num * PID_TUNER_Q15 * PID_TUNER_Q8
                          / ((int64_t)process_gain_q15 * den));
        kp_q8 = clamp_q8(kp_q8);
    }

    int32_t ki_q8 = 0;
    int64_t ti_ms = (int64_t)T_ms + half_L;   /* Ti = T + L/2 */
    if (ti_ms > 0 && kp_q8 != 0) {
        /* ki_q8 = Ki*256 = (Kp/Ti)*256 = kp_q8 / Ti, rounded to nearest
         * (see round_div_i64) rather than truncated toward zero. */
        ki_q8 = round_div_i64((int64_t)kp_q8, ti_ms);
    }

    int32_t kd_q8 = 0;
    int64_t den2 = (int64_t)2 * (int64_t)T_ms + (int64_t)L_ms;
    if (den2 > 0) {
        int64_t td_ms = ((int64_t)T_ms * (int64_t)L_ms) / den2;  /* Td = T*L/(2T+L) */
        /* kd_q8 = Kd*256 = (Kp*Td)*256 = kp_q8 * Td.
         * kd carries the process time constant (units output*ms/error), so
         * it can exceed the ±128 Q8 range of kp/ki; clamp to int32 sanity. */
        kd_q8 = (int32_t)((int64_t)kp_q8 * td_ms);
        if (kd_q8 > 16777216) kd_q8 = 16777216;   /* 2^24 */
        if (kd_q8 < -16777216) kd_q8 = -16777216;
    }

    out->kp_q8 = kp_q8;
    out->ki_q8 = ki_q8;
    out->kd_q8 = kd_q8;
    out->status = PID_TUNE_OK;
}

/* Map a (possibly partial) FOPDT model to Ziegler-Nichols open-loop gains.
 * ZN open-loop for K*e^(-Ls)/(Ts+1):
 *   Kp = 1.2 * T / (K * L)
 *   Ti = 2 * L
 *   Td = 0.5 * L
 * Returns gains via out; out->status set to PID_TUNE_ZN_FALLBACK. */
static void zn_tune(const int32_t process_gain_q15,
                    uint32_t T_ms,
                    uint32_t L_ms,
                    pid_tune_result_t *out) {
    int32_t kp_q8 = 0;
    if (process_gain_q15 != 0 && L_ms != 0) {
        /* kp_q8 = 256 * 1.2*T/(K*L) = 6*T*Q15*256 / (5*K_q15*L) */
        kp_q8 = (int32_t)((int64_t)6 * T_ms * PID_TUNER_Q15 * PID_TUNER_Q8
                          / ((int64_t)5 * process_gain_q15 * L_ms));
        kp_q8 = clamp_q8(kp_q8);
    }

    int32_t ki_q8 = 0;
    int64_t ti_ms = (int64_t)2 * (int64_t)L_ms;   /* Ti = 2L */
    if (ti_ms > 0 && kp_q8 != 0) {
        /* ki_q8 = Ki*256 = (Kp/Ti)*256 = kp_q8 / Ti, rounded to nearest
         * (see round_div_i64) rather than truncated toward zero. */
        ki_q8 = round_div_i64((int64_t)kp_q8, ti_ms);
    }

    int32_t kd_q8 = 0;
    int64_t td_ms = (int64_t)L_ms / 2;            /* Td = 0.5L */
    if (td_ms > 0 && kp_q8 != 0) {
        /* kd_q8 = Kd*256 = (Kp*Td)*256 = kp_q8 * Td (same convention as IMC) */
        kd_q8 = (int32_t)((int64_t)kp_q8 * td_ms);
        if (kd_q8 > 16777216) kd_q8 = 16777216;   /* 2^24 */
        if (kd_q8 < -16777216) kd_q8 = -16777216;
    }

    out->kp_q8 = kp_q8;
    out->ki_q8 = ki_q8;
    out->kd_q8 = kd_q8;
    out->status = PID_TUNE_ZN_FALLBACK;
}

/* Least-squares slope (units per ms) of samples[start..end] (inclusive),
 * scaled by 1000 to units/s. Returns 0 on degenerate input. */
static int64_t ls_slope(const pid_tuner_state_t *state,
                        uint16_t start, uint16_t end) {
    if (end <= start || end >= state->sample_count) return 0;
    uint16_t n = end - start + 1;
    int64_t sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (uint16_t i = start; i <= end; i++) {
        int64_t x = state->samples[i].t_ms;
        int64_t y = state->samples[i].y;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    int64_t den = (int64_t)n * sxx - sx * sx;
    if (den == 0) return 0;
    return (((int64_t)n * sxy - sx * sy) * 1000) / den;
}

void pid_tuner_tune(pid_tuner_state_t *state, pid_tune_result_t *result) {
    if (!state || !result || !state->initialized) return;
    if (state->sample_count < PID_TUNER_MIN_SAMPLES) {
        result->status = PID_TUNE_INSUFFICIENT;
        return;
    }

    /* ---- steady-state output: average the final 20% of samples --------- */
    uint16_t tail_n = state->sample_count / 5;
    if (tail_n == 0) tail_n = 1;
    int64_t tail_sum = 0;
    for (uint16_t i = state->sample_count - tail_n; i < state->sample_count; i++) {
        tail_sum += state->samples[i].y;
    }
    int32_t y_ss = (int32_t)(tail_sum / tail_n);

    int32_t dy = y_ss - (int32_t)state->y_baseline;
    int32_t dy_abs = (dy < 0) ? -dy : dy;

    /* No detectable response -> insufficient.
     * dy_abs >= 20 is ~2% of a 0-1000 sensor range: below this, the
     * permille fraction thresholds (283/1000 etc.) lose integer resolution
     * and the fit degenerates. Treat sub-2% responses as noise. */
    if (dy_abs < 20 || state->step_amplitude == 0) {
        result->status = PID_TUNE_INSUFFICIENT;
        return;
    }

    /* ---- process gain K (Q15) ------------------------------------------ */
    int64_t gain_num = (int64_t)dy * PID_TUNER_Q15;
    int32_t process_gain_q15 = (int32_t)(gain_num / (int64_t)state->step_amplitude);
    if (process_gain_q15 > 32767) process_gain_q15 = 32767;
    if (process_gain_q15 < -32768) process_gain_q15 = -32768;

    /* ---- dead time L: first crossing of the noise band ---------------- */
    uint32_t dead_time_ms = 0;
    {
        int64_t noise_band = (int64_t)dy_abs * PID_TUNER_NOISE_BAND_PERMILLE / 1000;
        if (noise_band < 1) noise_band = 1;
        for (uint16_t i = 0; i < state->sample_count; i++) {
            int64_t mag = (int64_t)state->samples[i].y - (int64_t)state->y_baseline;
            if (mag < 0) mag = -mag;
            if (mag >= noise_band) {
                dead_time_ms = state->samples[i].t_ms;
                break;
            }
        }
    }

    /* ---- time constant T: average of multi-point fit ------------------- */
    /* Fractions and their time-constant multipliers:
     *   28.3% -> t-L = T/3 ; 63.2% -> t-L = T ; 86.5% -> t-L = 2T ;
     *   95.0% -> t-L = 3T
     * Encoded as t_mults/t_divs ratios: (t_cross - L) * mult / div. */
    static const uint16_t fracs[PID_TUNER_FRAC_COUNT] = {283, 632, 865, 950};
    static const uint16_t t_mults[PID_TUNER_FRAC_COUNT] = {3, 1, 1, 1};
    static const uint16_t t_divs[PID_TUNER_FRAC_COUNT] = {1, 1, 2, 3};

    int64_t T_sum = 0;
    uint16_t T_count = 0;

    for (uint16_t i = 0; i < PID_TUNER_FRAC_COUNT; i++) {
        uint32_t t_cross = find_crossing_time(state, state->y_baseline, dy_abs, fracs[i]);
        if (t_cross == 0 || t_cross <= dead_time_ms) continue;
        uint32_t dt_after_L = t_cross - dead_time_ms;
        /* T = (t_cross - L) * mult / div */
        int64_t T_est = (int64_t)dt_after_L * (int64_t)t_mults[i] / (int64_t)t_divs[i];
        if (T_est > 0) {
            T_sum += T_est;
            T_count++;
        }
    }

    uint32_t T_ms = 0;
    if (T_count > 0) {
        T_ms = (uint32_t)(T_sum / T_count);
    }

    /* ---- decide path --------------------------------------------------- */
    if (T_ms == 0 || dead_time_ms == 0) {
        /* No reliable model; keep collecting */
        result->status = PID_TUNE_INSUFFICIENT;
        result->process_gain_q15 = process_gain_q15;
        result->time_const_ms = T_ms;
        result->dead_time_ms = dead_time_ms;
        result->sample_count = state->sample_count;
        return;
    }

    result->process_gain_q15 = process_gain_q15;
    result->time_const_ms = T_ms;
    result->dead_time_ms = dead_time_ms;
    result->sample_count = state->sample_count;

    /* ---- settled test (least-squares slope ratio) ----------------------
     * A reaction curve still rising has a large terminal slope; a settled
     * one is flat. Compare least-squares slope over the last 25% of samples
     * to the initial response slope (just after the dead time).
     *
     * For a FOPDT curve, slope(t)/slope(0) = e^(-(t-L)/T):
     *   at 3T  -> 4.98%   (95% settled -> IMC full fit)
     *   at 1.5T-> 22.3%   (78% settled -> ZN fallback)
     * A 10% threshold cleanly separates the two. Slope is scale-free.
     *
     * This avoids the pitfall of comparing against the estimated steady
     * state: the tail average is biased low while the curve is rising, so
     * a fraction-based test can wrongly classify a partial response as
     * complete. */
    uint16_t tail_start = state->sample_count - state->sample_count / 4;
    if (tail_start >= state->sample_count) tail_start = state->sample_count - 1;
    int64_t slope_terminal = ls_slope(state, tail_start,
                                      state->sample_count - 1);

    /* Initial slope: fit over the rise region between the dead time and the
     * 63.2% crossing (t-L = T), which captures the steepest part. */
    int64_t slope_initial = 0;
    {
        uint32_t t632 = find_crossing_time(state, state->y_baseline, dy_abs, 632);
        uint32_t t_end = (t632 > dead_time_ms) ? t632
                                               : state->samples[state->sample_count - 1].t_ms;
        uint16_t iA = 0, iB = 0;
        for (uint16_t i = 0; i < state->sample_count; i++) {
            if (state->samples[i].t_ms >= dead_time_ms) { iA = i; break; }
        }
        for (uint16_t i = state->sample_count - 1; i > 0; i--) {
            if (state->samples[i].t_ms <= t_end) { iB = i; break; }
        }
        if (iB > iA) {
            slope_initial = ls_slope(state, iA, iB);
        }
        if (slope_initial < 0) slope_initial = -slope_initial;
        if (slope_terminal < 0) slope_terminal = -slope_terminal;
    }

    if (slope_initial > 0 &&
        slope_terminal <= (slope_initial * 10) / 100) {
        /* Settled: full data-driven (IMC) fit */
        imc_tune(process_gain_q15, T_ms, dead_time_ms, result);
    } else if (T_ms > 0 && dead_time_ms > 0) {
        /* Partial curve but T and L are estimable: Ziegler-Nichols fallback */
        zn_tune(process_gain_q15, T_ms, dead_time_ms, result);
    } else {
        result->status = PID_TUNE_INSUFFICIENT;
    }
}

/* ---- PID controller ---------------------------------------------------- */

void pid_controller_init(pid_controller_t *c,
                         int32_t kp_q8, int32_t ki_q8, int32_t kd_q8,
                         int32_t out_min, int32_t out_max) {
    if (!c) return;
    memset(c, 0, sizeof(pid_controller_t));
    c->kp_q8 = kp_q8;
    c->ki_q8 = ki_q8;
    c->kd_q8 = kd_q8;
    c->out_min = out_min;
    c->out_max = out_max;
    c->initialized = true;
}

int32_t pid_controller_update(pid_controller_t *c,
                              int16_t setpoint,
                              int16_t y,
                              uint32_t dt_ms) {
    if (!c || !c->initialized) return 0;
    if (dt_ms == 0) dt_ms = 1;

    int32_t error = (int32_t)setpoint - (int32_t)y;

    /* Proportional: kp_q8 * error / 256 */
    int64_t p_term = ((int64_t)c->kp_q8 * error) >> 8;

    /* Integral accumulator (error * ms), with bounded magnitude to avoid
     * overflow across long runs. Clamped inside the conditional-integration
     * logic below. */
    c->integral += (int64_t)error * (int64_t)dt_ms;
    if (c->integral >  33554432) c->integral =  33554432;  /* 2^25 */
    if (c->integral < -33554432) c->integral = -33554432;

    int64_t i_term = ((int64_t)c->ki_q8 * c->integral) >> 8;

    /* Derivative (per ms): kd_q8 * (error - prev_error) / dt / 256 */
    int32_t d_error = error - (int32_t)c->prev_error;
    int64_t d_term = ((int64_t)c->kd_q8 * d_error) / (int64_t)dt_ms;
    d_term = d_term >> 8;

    int64_t out = p_term + i_term + d_term;

    /* Conditional integration (anti-windup): freeze integral while saturated
     * in the direction the integral is pushing. */
    if (out > c->out_max) {
        if (i_term > 0) {
            c->integral -= ((int64_t)error * (int64_t)dt_ms);
        }
        out = c->out_max;
    } else if (out < c->out_min) {
        if (i_term < 0) {
            c->integral -= ((int64_t)error * (int64_t)dt_ms);
        }
        out = c->out_min;
    }

    c->prev_error = (int16_t)error;

    if (out > c->out_max) out = c->out_max;
    if (out < c->out_min) out = c->out_min;
    return (int32_t)out;
}

#endif /* PHASE_ENGINE_ENABLED */
