/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Data-Driven PID Auto-Tuning for Cortex-M0+
 *
 * Learns PID coefficients (Kp/Ki/Kd) from step-response data instead of
 * manual Ziegler-Nichols tuning. Workflow:
 *
 *   1. Begin a step test: apply a step to the plant, record the process
 *      output y(t) at each control tick.
 *   2. Fit a First-Order-Plus-Dead-Time (FOPDT) model
 *          K * e^(-L s) / (T s + 1)
 *      from the measured reaction curve using integer arithmetic:
 *          K = steady-state output change / input step
 *          L = dead time (first output crossing above the noise band)
 *          T = time constant (averaged over the 28.3/63.2/86.5/95% curve
 *              points, so the whole response informs the fit, not a
 *              single point)
 *   3. Map the fitted model to PID gains:
 *          Primary: IMC (lambda) tuning — robust, uses the full curve
 *          Fallback: Ziegler-Nichols open-loop — when the response data
 *                    is insufficient for a full fit (did not reach 95%
 *                    of steady state), uses the partial reaction curve.
 *
 * All math is integer / fixed-point. PID gains are Q8 (1/256 resolution):
 * kp_q8 and ki_q8 have a nominal range of ±128; kd_q8 carries the process
 * time constant (units output*ms/error) and is clamped to ±2^24. Process
 * gain is Q15. No floats, no FPU; int64 intermediates only (Cortex-M0+
 * UMULL gives 32x32->64 in 32 cycles; software divide is acceptable off
 * the hot path).
 *
 * References:
 *   - IMC tuning: Rivera, Morari, Skogestad (1986), "Internal Model Control:
 *     PID Controller Design"
 *   - Ziegler-Nichols open-loop: Ziegler & Nichols (1942), "Optimum Settings
 *     for Automatic Controllers"
 *   - Applied from vault: control-theory/2026-07-16-mipt-ml-pid-tuning-ru
 *
 * Stability:
 *   - IMC gains are BIBO-stable for FOPDT plants when lambda > 0 (tuning
 *     knob separates speed from robustness).
 *   - The fit is bounded: K, L, T are clamped to physically meaningful
 *     ranges; degenerate fits (no crossing, zero gain, non-monotonic
 *     response) return PID_TUNE_INSUFFICIENT instead of producing unsafe
 *     gains.
 *   - The PID controller clamps output and uses conditional integration
 *     (anti-windup) so an untuned or noisy loop cannot run away.
 */

#ifndef PID_TUNER_H_
#define PID_TUNER_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef PHASE_ENGINE_ENABLED

/* Max samples collected during one step test. 64 samples x 6 bytes =
 * 384 bytes of state. Only instantiated by callers that use auto-tuning. */
#define PID_TUNER_MAX_SAMPLES 64
/* Minimum samples before a fit is attempted. */
#define PID_TUNER_MIN_SAMPLES 8
/* Noise band as a fraction of the total response (x1000): 3% of dy. */
#define PID_TUNER_NOISE_BAND_PERMILLE 30

/* Fraction thresholds for the multi-point time-constant fit, in permille.
 * Each maps to a known multiple of T for a first-order response:
 *   28.3% -> T/3, 63.2% -> T, 86.5% -> 2T, 95.0% -> 3T */
#define PID_TUNER_FRAC_COUNT 4

/* Fixed-point scalings. */
#define PID_TUNER_Q8   256
#define PID_TUNER_Q15  32768

typedef enum {
    PID_TUNE_OK = 0,          /* Fit succeeded; gains are IMC-tuned */
    PID_TUNE_ZN_FALLBACK = 1, /* Data insufficient for full fit; Ziegler-Nichols used */
    PID_TUNE_INSUFFICIENT = 2 /* Not enough / invalid response data yet */
} pid_tune_status_t;

typedef struct {
    uint32_t t_ms;   /* Time since step start */
    int16_t  y;      /* Process output sample */
} pid_tune_sample_t;

/* Result of an auto-tune run. Gains are Q8 fixed point (value / 256). */
typedef struct {
    int32_t kp_q8;         /* Proportional gain (Q8) */
    int32_t ki_q8;         /* Integral gain per ms (Q8) */
    int32_t kd_q8;         /* Derivative gain per ms (Q8) */
    pid_tune_status_t status;
    /* Fitted FOPDT model (for diagnostics / logging) */
    int32_t process_gain_q15; /* K (Q15) */
    uint32_t time_const_ms;   /* T */
    uint32_t dead_time_ms;    /* L */
    uint16_t sample_count;    /* Samples used */
} pid_tune_result_t;

/* PID controller that consumes tuned gains (Q8), integer math. */
typedef struct {
    int32_t kp_q8;
    int32_t ki_q8;
    int32_t kd_q8;
    int32_t integral;        /* Integral accumulator (scaled) */
    int16_t prev_error;      /* Previous error for derivative */
    int32_t out_min;         /* Output clamp (inclusive) */
    int32_t out_max;         /* Output clamp (inclusive) */
    bool     initialized;
} pid_controller_t;

/* Step-test recorder + tuner state. */
typedef struct {
    pid_tune_sample_t samples[PID_TUNER_MAX_SAMPLES];
    uint16_t sample_count;
    int32_t  step_amplitude;  /* Input step magnitude (u units) */
    int16_t  y_baseline;      /* Output before the step */
    uint32_t step_start_ms;   /* For absolute-sample bookkeeping */
    bool     initialized;
} pid_tuner_state_t;

/**
 * Initialize the auto-tuner state. Call once before use.
 */
void pid_tuner_init(pid_tuner_state_t *state);

/**
 * Begin a step test.
 *
 * The caller applies an instantaneous step of magnitude @p step_amplitude
 * to the plant and starts feeding (t, y) samples via pid_tuner_add_sample().
 * @p y_baseline is the plant output immediately before the step.
 *
 * @param state         Tuner state
 * @param step_amplitude Input step magnitude (nonzero, signed)
 * @param y_baseline     Output before the step
 * @param step_start_ms  Monotonic time of the step (for bookkeeping)
 */
void pid_tuner_begin(pid_tuner_state_t *state,
                     int32_t step_amplitude,
                     int16_t y_baseline,
                     uint32_t step_start_ms);

/**
 * Record one process-output sample during the step test.
 *
 * Samples are kept in a fixed ring buffer; once full, the oldest are
 * discarded. @p t_ms is relative to step start (0 = moment of step).
 *
 * @param state Tuner state
 * @param t_ms  Time since step start (ms)
 * @param y     Process output at t_ms
 */
void pid_tuner_add_sample(pid_tuner_state_t *state, uint32_t t_ms, int16_t y);

/**
 * Fit the FOPDT model from collected samples and compute PID gains.
 *
 * Primary path (PID_TUNE_OK): the response reached >= 95% of steady state;
 * K, L, T are identified and gains are IMC (lambda) tuned with
 * lambda = 0.25 * T (aggressive-but-robust, see header refs).
 *
 * Fallback (PID_TUNE_ZN_FALLBACK): the response did not reach 95% but did
 * pass the 63.2% point, so T is estimable from the partial curve; gains are
 * Ziegler-Nichols open-loop tuned from the available K/L/T.
 *
 * Insufficient (PID_TUNE_INSUFFICIENT): fewer than PID_TUNER_MIN_SAMPLES,
 * zero/near-zero gain, or no detectable response. The result gains are left
 * at their previous values (or zero) and the caller should keep collecting.
 *
 * @param state  Tuner state
 * @param result Output gains + fitted model (status in result->status)
 */
void pid_tuner_tune(pid_tuner_state_t *state, pid_tune_result_t *result);

/**
 * Initialize a PID controller with Q8 gains.
 *
 * @param c       Controller state
 * @param kp_q8   Proportional gain (Q8)
 * @param ki_q8   Integral gain per ms (Q8)
 * @param kd_q8   Derivative gain per ms (Q8)
 * @param out_min Minimum output (clamp)
 * @param out_max Maximum output (clamp)
 */
void pid_controller_init(pid_controller_t *c,
                         int32_t kp_q8, int32_t ki_q8, int32_t kd_q8,
                         int32_t out_min, int32_t out_max);

/**
 * Compute one PID output sample.
 *
 * Standard positional PID with conditional integration (anti-windup):
 * the integral term is frozen while the output is saturated and the
 * un-clamped output would push further into the same limit.
 *
 * @param c         Controller state (integral/prev_error updated in place)
 * @param setpoint  Desired process output
 * @param y         Current process output
 * @param dt_ms     Time since last update (ms), nonzero
 * @return          Control output, clamped to [out_min, out_max]
 */
int32_t pid_controller_update(pid_controller_t *c,
                              int16_t setpoint,
                              int16_t y,
                              uint32_t dt_ms);

#endif /* PHASE_ENGINE_ENABLED */
#endif /* PID_TUNER_H_ */
