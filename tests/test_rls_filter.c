/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Host dogfood harness for the adaptive EMA (AEMA) and diagonal-P RLS
 * filters in lib/phase/rls_filter.c.
 *
 * Neither filter had any test coverage before this file. AEMA is live in
 * the sensor pipeline (sensors.c); RLS is implemented but not yet wired
 * in (see rls_filter.h's Stability analysis section). These checks cover:
 *   1. AEMA alpha stays within [alpha_min, alpha_max] and the filter
 *      tracks a step change.
 *   2. RLS diagonal P stays within its documented bounds (never negative,
 *      never exceeds the 2^25 clamp) across many iterations, for both a
 *      near-stationary signal and a large-amplitude signal near the
 *      int16 range that stresses the fixed-point arithmetic.
 *   3. RLS prediction error trends down over time on a learnable signal
 *      (sanity check that the corrected P-update still produces a
 *      functioning adaptive predictor, not just "doesn't crash").
 *
 * Build (host, no hardware deps):
 *   gcc -DPHASE_ENGINE_ENABLED -Ilib/phase -o /tmp/rls_dogfood \
 *       tests/test_rls_filter.c lib/phase/rls_filter.c
 * Run: bash tests/test_rls_filter.sh
 */

#include <stdio.h>
#include <stdlib.h>

#include "rls_filter.h"

#define RLS_P_CLAMP_MAX 33554432 /* 2^25, must match rls_filter.c */

static int failures = 0;

#define CHECK(cond, msg, ...)                                              \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("  [FAIL] " msg "\n", ##__VA_ARGS__);                   \
            failures++;                                                    \
        }                                                                  \
    } while (0)

static int16_t absi(int16_t x) { return x < 0 ? (int16_t)(-x) : x; }

/* ---- AEMA -------------------------------------------------------------- */

static void test_aema_bounds_and_tracking(void) {
    printf("\n==== AEMA: alpha bounds + step tracking ====\n");

    aema_state_t s;
    aema_init(&s); /* alpha_min=2048 (0.0625), alpha_max=16384 (0.5), scale=100 */

    /* First sample initializes ema directly (no convergence needed). */
    int16_t baseline = 500;
    (void)aema_update(&s, baseline);
    CHECK(aema_get(&s) == baseline, "initial ema=%d, want %d", aema_get(&s), baseline);

    /* A large jump should saturate alpha at alpha_max. */
    int16_t target = 1500;
    int32_t alpha_at_jump = aema_get_alpha(&s, target);
    CHECK(alpha_at_jump == AEMA_ALPHA_MAX_Q15,
          "alpha at large step = %d, want alpha_max=%d", alpha_at_jump, AEMA_ALPHA_MAX_Q15);

    /* Track the step for many samples; alpha must stay in bounds every
     * step, and the filter should converge close to the target as the
     * error shrinks (alpha relaxes toward alpha_min but never reaches
     * exactly 0, so convergence is asymptotic, not exact -- assert
     * "close", not "equal"). */
    int16_t v = baseline;
    for (int i = 0; i < 200; i++) {
        int32_t alpha = aema_get_alpha(&s, target);
        CHECK(alpha >= AEMA_ALPHA_MIN_Q15 && alpha <= AEMA_ALPHA_MAX_Q15,
              "iter %d: alpha=%d out of [%d,%d]", i, alpha, AEMA_ALPHA_MIN_Q15, AEMA_ALPHA_MAX_Q15);
        v = aema_update(&s, target);
    }
    int16_t final_err = absi((int16_t)(target - v));
    printf("  after 200 samples: ema=%d target=%d err=%d\n", v, target, final_err);
    /* The shift-based correction (alpha*error)>>15 truncates to exactly 0
     * once alpha*error < 32768 -- at alpha_min=2048 that's error < 16, a
     * real fixed-point convergence floor inherent to this implementation
     * (pre-existing, not something this test suite's fix touches), not a
     * bug. Assert convergence to within that floor, not to near-zero. */
    CHECK(final_err <= 16, "did not converge within the alpha_min fixed-point floor (err=%d)", final_err);
}

/* ---- RLS ---------------------------------------------------------------- */

/* Run RLS on `n` samples of the given signal, checking P stays bounded on
 * every single iteration. Returns the mean absolute prediction error over
 * the last `tail` samples (for the caller to check learning progress). */
static double run_rls_bounded(rls_state_t *rls, const int16_t *signal, int n, int tail) {
    double tail_err_sum = 0.0;
    int tail_n = 0;

    for (int i = 0; i < n; i++) {
        int16_t predicted = rls_get(rls);
        int16_t err = rls_get_error(rls, signal[i]);
        (void)rls_update(rls, signal[i]);

        for (int k = 0; k < RLS_ORDER; k++) {
            CHECK(rls->p[k] >= 0, "iter %d tap %d: p=%d went negative", i, k, rls->p[k]);
            CHECK(rls->p[k] <= RLS_P_CLAMP_MAX,
                  "iter %d tap %d: p=%d exceeds clamp %d", i, k, rls->p[k], RLS_P_CLAMP_MAX);
        }

        if (i >= n - tail) {
            tail_err_sum += absi(err);
            tail_n++;
            (void)predicted;
        }
    }
    return tail_n > 0 ? tail_err_sum / tail_n : -1.0;
}

static void test_rls_bounded_stationary(void) {
    printf("\n==== RLS: P stays bounded on a near-stationary signal ====\n");
    rls_state_t rls;
    rls_init(&rls);

    int16_t signal[300];
    for (int i = 0; i < 300; i++) {
        /* Constant signal with tiny +/-2 dither, like a quiet sensor. */
        signal[i] = (int16_t)(500 + ((i % 4) - 2));
    }
    double tail_err = run_rls_bounded(&rls, signal, 300, 50);
    printf("  final p = [%d, %d], mean |err| last 50 = %.2f\n", rls.p[0], rls.p[1], tail_err);
    CHECK(tail_err < 5.0, "did not settle on a near-constant signal (mean |err|=%.2f)", tail_err);
}

static void test_rls_bounded_large_amplitude(void) {
    printf("\n==== RLS: P stays bounded under large-amplitude input ====\n");
    rls_state_t rls;
    rls_init(&rls);

    /* Values near the int16 range (the worst case identified in the
     * P-update fix: large x[] combined with p[] near its clamp ceiling
     * is exactly where the old k[i]^2*x[i]^2 formula and the int32_t
     * px2[] truncation were both most exposed). Alternate sign to also
     * exercise the negative-input path. */
    int16_t signal[500];
    for (int i = 0; i < 500; i++) {
        signal[i] = (int16_t)((i % 2 == 0) ? 30000 : -30000);
    }
    (void)run_rls_bounded(&rls, signal, 500, 50);
    printf("  final p = [%d, %d] (bounds already checked per-iteration above)\n",
           rls.p[0], rls.p[1]);
}

static void test_rls_learns_ramp(void) {
    printf("\n==== RLS: prediction error trends down on a learnable ramp ====\n");
    rls_state_t rls;
    rls_init(&rls);

    int16_t signal[400];
    for (int i = 0; i < 400; i++) {
        /* Slow linear ramp with small noise -- a 2-tap linear predictor
         * should learn to track this. */
        signal[i] = (int16_t)(200 + i / 2 + ((i * 37) % 7) - 3);
    }

    double early_err_sum = 0.0, late_err_sum = 0.0;
    for (int i = 0; i < 400; i++) {
        int16_t err = rls_get_error(&rls, signal[i]);
        (void)rls_update(&rls, signal[i]);
        for (int k = 0; k < RLS_ORDER; k++) {
            CHECK(rls.p[k] >= 0 && rls.p[k] <= RLS_P_CLAMP_MAX,
                  "iter %d tap %d: p=%d out of bounds", i, k, rls.p[k]);
        }
        if (i < 50) early_err_sum += absi(err);
        if (i >= 350) late_err_sum += absi(err);
    }
    double early_mean = early_err_sum / 50.0;
    double late_mean = late_err_sum / 50.0;
    printf("  mean |err| first 50 = %.2f, last 50 = %.2f\n", early_mean, late_mean);
    CHECK(late_mean < early_mean,
          "prediction error did not improve (early=%.2f late=%.2f)", early_mean, late_mean);
}

int main(void) {
    test_aema_bounds_and_tracking();
    test_rls_bounded_stationary();
    test_rls_bounded_large_amplitude();
    test_rls_learns_ramp();

    printf("\n%d check(s) failed.\n", failures);
    return failures == 0 ? 0 : 1;
}
