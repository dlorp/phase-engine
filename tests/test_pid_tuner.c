/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: PID Auto-Tuner Dogfood Harness (host build)
 *
 * Simulates a First-Order-Plus-Dead-Time (FOPDT) plant, runs the data-driven
 * auto-tuner on its step response, then closes the loop with the tuned gains
 * and reports the closed-loop outcome (settling time, overshoot, steady-state
 * error).
 *
 * Build (host, no hardware deps):
 *   gcc -DPHASE_ENGINE_ENABLED -Ilib/phase -o /tmp/pid_dogfood \
 *       tests/test_pid_tuner.c lib/phase/pid_tuner.c
 * Run: /tmp/pid_dogfood
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "pid_tuner.h"

/* --------------------------------------------------------------------------
 * FOPDT plant simulator
 *   G(s) = K * e^(-L s) / (T s + 1)
 * Discrete forward-Euler with dead-time FIFO. Integer output.
 * -------------------------------------------------------------------------- */
typedef struct {
    double K;
    double T;
    double L;
    double y;             /* current output */
    double *delay_fifo;   /* dead-time FIFO */
    int    fifo_len;
    int    fifo_idx;
} plant_t;

static void plant_init(plant_t *p, double K, double T, double L, double dt) {
    p->K = K;
    p->T = T;
    p->L = L;
    p->y = 0.0;
    p->fifo_len = (int)ceil(L / dt);
    if (p->fifo_len < 1) p->fifo_len = 1;
    p->delay_fifo = calloc((size_t)p->fifo_len, sizeof(double));
    p->fifo_idx = 0;
}

/* Step the plant one dt with input u. Returns new output. */
static double plant_step(plant_t *p, double u, double dt) {
    /* delayed input: FIFO */
    double u_delayed = p->delay_fifo[p->fifo_idx];
    p->delay_fifo[p->fifo_idx] = u;
    p->fifo_idx = (p->fifo_idx + 1) % p->fifo_len;

    double dy = (dt / p->T) * (p->K * u_delayed - p->y);
    p->y += dy;
    return p->y;
}

/* --------------------------------------------------------------------------
 * Scenario runner: simulate step test -> tune -> closed loop -> metrics
 * -------------------------------------------------------------------------- */
typedef struct {
    const char *name;
    double plant_K, plant_T, plant_L;
    double dt;
    double step_amp;
    double setpoint;
    int    test_samples;      /* step-response samples fed to tuner */
    int    loop_samples;      /* closed-loop samples */
} scenario_t;

static double absd(double x) { return x < 0 ? -x : x; }

/* Return 0 = pass, nonzero = fail */
static int run_scenario(const scenario_t *s) {
    printf("\n==== %s ====\n", s->name);
    printf("Plant: K=%.2f T=%.1fs L=%.1fs  dt=%.3fs  step=%.1f\n",
           s->plant_K, s->plant_T, s->plant_L, s->dt, s->step_amp);

    plant_t plant;
    plant_init(&plant, s->plant_K, s->plant_T, s->plant_L, s->dt);

    pid_tuner_state_t tuner;
    pid_tuner_init(&tuner);

    /* Step test: plant at rest y=0, apply step u=step_amp */
    double u = s->step_amp;
    pid_tuner_begin(&tuner, (int32_t)(s->step_amp), (int16_t)0, 0);

    double y;
    for (int i = 0; i < s->test_samples; i++) {
        y = plant_step(&plant, u, s->dt);
        pid_tuner_add_sample(&tuner, (uint32_t)(i * 1000 * s->dt), (int16_t)y);
    }

    pid_tune_result_t res;
    memset(&res, 0, sizeof(res));
    pid_tuner_tune(&tuner, &res);

    const char *status_str =
        (res.status == PID_TUNE_OK) ? "IMC (data-driven)" :
        (res.status == PID_TUNE_ZN_FALLBACK) ? "Ziegler-Nichols fallback" :
        "INSUFFICIENT";
    printf("Tune status : %s\n", status_str);
    printf("Fitted model: K=%.3f (true %.3f)  T=%.0fms (true %.0f)  L=%.0fms (true %.0f)\n",
           res.process_gain_q15 / 32768.0, s->plant_K,
           (double)res.time_const_ms, s->plant_T * 1000.0,
           (double)res.dead_time_ms, s->plant_L * 1000.0);
    printf("Tuned gains : Kp=%.3f  Ki=%.6f/ms  Kd=%.3f*ms\n",
           res.kp_q8 / 256.0, res.ki_q8 / 256.0, res.kd_q8 / 256.0);

    if (res.status == PID_TUNE_INSUFFICIENT) {
        printf("RESULT: insufficient data (expected only for the negative test)\n");
        free(plant.delay_fifo);
        return (strstr(s->name, "insufficient") != NULL) ? 0 : 1;
    }

    /* Regression guard: ki_q8 must be Kp/Ti rounded to nearest, not
     * truncated toward zero. A truncating division silently zeroes out
     * small-but-nonzero integral gains (see round_div_i64 in pid_tuner.c);
     * this test previously had no assertion on any fitted/tuned value. */
    {
        int64_t ti_ms = (res.status == PID_TUNE_OK)
            ? (int64_t)res.time_const_ms + (int64_t)res.dead_time_ms / 2
            : (int64_t)2 * (int64_t)res.dead_time_ms;
        if (ti_ms > 0) {
            double expected = (double)res.kp_q8 / (double)ti_ms;
            double diff = absd((double)res.ki_q8 - expected);
            if (diff > 0.5 + 1e-9) {
                printf("FAIL: ki_q8=%d is not Kp/Ti rounded to nearest "
                       "(kp_q8=%d Ti=%lldms, expected ~= %.3f)\n",
                       res.ki_q8, res.kp_q8, (long long)ti_ms, expected);
                free(plant.delay_fifo);
                return 1;
            }
        }
    }

    /* ---- closed loop with tuned gains ---------------------------------- */
    pid_controller_t c;
    pid_controller_init(&c, res.kp_q8, res.ki_q8, res.kd_q8,
                        -5000, 5000);

    plant_t cl_plant;
    plant_init(&cl_plant, s->plant_K, s->plant_T, s->plant_L, s->dt);

    /* restart tuner-less; drive plant toward setpoint from y=0 */
    double out = 0.0;
    double settle_thresh = 0.05 * s->setpoint;
    double settle_time = -1.0;
    double max_overshoot = 0.0;  /* max y above setpoint (true overshoot) */
    double ss_err = 0.0;
    int    n_ss = 0;
    double *y_hist = calloc((size_t)s->loop_samples, sizeof(double));
    if (!y_hist) { free(plant.delay_fifo); return 1; }

    for (int i = 1; i <= s->loop_samples; i++) {
        double y = plant_step(&cl_plant, out, s->dt);
        y_hist[i - 1] = y;
        out = pid_controller_update(&c, (int16_t)s->setpoint, (int16_t)y,
                                    (uint32_t)(1000 * s->dt));

        double over = y - s->setpoint;
        if (over > max_overshoot) max_overshoot = over;

        /* steady-state window: final 200 samples */
        if (i > s->loop_samples - 200) {
            ss_err += absd(y - s->setpoint);
            n_ss++;
        }
    }

    /* settling time: last index where |y - setpoint| leaves the 5% band
     * for the final time (2% version is the standard; use 5% here). */
    for (int i = s->loop_samples - 1; i >= 0; i--) {
        if (absd(y_hist[i] - s->setpoint) > settle_thresh) {
            settle_time = (i + 1) * 1000.0 * s->dt;  /* ms after start */
            break;
        }
    }
    if (settle_time < 0) settle_time = 0.0;

    double overshoot_pct = (s->setpoint > 0) ? (max_overshoot / s->setpoint) * 100.0 : 0.0;
    double final_err = (n_ss > 0) ? ss_err / n_ss : -1.0;

    printf("Closed loop: settle=%.0fms  overshoot=%.1f%%  final err=%.2f\n",
           settle_time, overshoot_pct, final_err);

    free(y_hist);
    free(plant.delay_fifo);
    free(cl_plant.delay_fifo);
    return 0;
}

int main(void) {
    int failures = 0;

    /* 1. Data-driven primary path: fast plant, full response to 95%+.
     * Outputs are in sensor scale (0-1000). K=0.2: step 1000 -> y_ss=200.
     * 64 samples @ 50ms = 3.2s; with T=1s, L=0.2s that passes 95% of the
     * response (3.2s - 0.2s = 3T) -> IMC path. Buffer is exactly 64. */
    scenario_t s1 = {
        "data-driven IMC tune (full response)",
        0.2, 1.0, 0.2,   /* K=0.2, T=1s, L=0.2s */
        0.05,            /* dt */
        1000.0,          /* step amplitude (control units) */
        500.0,           /* setpoint for closed loop */
        64,              /* test samples: 64*50ms = 3.2s */
        3000             /* loop samples */
    };
    failures += run_scenario(&s1);

    /* 2. Ziegler-Nichols fallback: response cut short before 95% but past 63.2%.
     * T=2s, L=0.3s. 64*50ms=3.2s -> (3.2-0.3)/2 = 1.45T -> ~76% of response
     * -> ZN path (>=63.2%, <95%). */
    scenario_t s2 = {
        "ZN fallback (partial response, no 95% settle)",
        0.2, 2.0, 0.3,
        0.05,
        1000.0,
        500.0,
        64,              /* test samples: 3.2s, ~76% of response */
        3000
    };
    failures += run_scenario(&s2);

    /* 3. Insufficient data: too few samples */
    scenario_t s3 = {
        "insufficient data (too few samples)",
        0.2, 1.0, 0.2,
        0.05,
        1000.0,
        500.0,
        4,               /* below PID_TUNER_MIN_SAMPLES (8) */
        3000
    };
    failures += run_scenario(&s3);

    printf("\n%d scenario(s) failed.\n", failures);
    return failures;
}
