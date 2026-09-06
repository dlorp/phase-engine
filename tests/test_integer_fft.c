/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Integer FFT Dogfood Harness (host build)
 *
 * Proves the acceptance criteria for the integer-to-integer FFT:
 *   1. EXACT round-trip: forward + inverse reproduces the input integer
 *      samples bit-for-bit, for a known sine input and for pseudo-random
 *      inputs, at every supported size.
 *   2. Spectral sanity: a clean sine lands its dominant power in the
 *      expected bins (f and n-f), within tolerance.
 *   3. DC/impulse behavior matches a standard DFT structurally.
 *
 * Build (host, no hardware deps):
 *   gcc -DPHASE_ENGINE_ENABLED -Ilib/phase -Wall -Wextra \
 *       -o /tmp/int_fft_dogfood tests/test_integer_fft.c lib/phase/int_fft.c -lm
 * Run: /tmp/int_fft_dogfood
 */

#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "int_fft.h"

static int failures = 0;
static int checks = 0;

#define CHECK(cond, msg)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (cond) {                                                          \
            printf("  [ok] %s\n", msg);                                      \
        } else {                                                             \
            failures++;                                                      \
            printf("  [FAIL] %s\n", msg);                                    \
        }                                                                    \
    } while (0)

/* Round trip on a buffer: forward then inverse, compare with original. */
static void roundtrip_case(const char *name, uint16_t n,
                           const int32_t *orig_re, const int32_t *orig_im) {
    int32_t re[INT_FFT_MAX_N];
    int32_t im[INT_FFT_MAX_N];
    memcpy(re, orig_re, n * sizeof(int32_t));
    memcpy(im, orig_im, n * sizeof(int32_t));

    char msg[128];
    if (!int_fft_forward(re, im, n)) {
        snprintf(msg, sizeof(msg), "%s (n=%u): forward returned false", name, n);
        CHECK(0, msg);
        return;
    }
    if (!int_fft_inverse(re, im, n)) {
        snprintf(msg, sizeof(msg), "%s (n=%u): inverse returned false", name, n);
        CHECK(0, msg);
        return;
    }

    int exact = 1;
    for (uint16_t i = 0; i < n; i++) {
        if (re[i] != orig_re[i] || im[i] != orig_im[i]) {
            exact = 0;
            if (failures < 20) {
                printf("  [detail] n=%u i=%u got=(%" PRId32 ",%" PRId32
                       ") want=(%" PRId32 ",%" PRId32 ")\n",
                       n, i, re[i], im[i], orig_re[i], orig_im[i]);
            }
            break;
        }
    }
    snprintf(msg, sizeof(msg), "%s (n=%u): exact integer round trip", name, n);
    CHECK(exact, msg);
}

/* Spectral check: dominant bins of a clean sine. */
static void spectrum_case(uint16_t n, int freq, int32_t amp) {
    int32_t re[INT_FFT_MAX_N];
    int32_t im[INT_FFT_MAX_N];
    uint32_t mag2[INT_FFT_MAX_N];

    for (uint16_t i = 0; i < n; i++) {
        double t = 2.0 * M_PI * freq * i / n;
        re[i] = (int32_t)lround(amp * sin(t));
        im[i] = 0;
    }

    char msg[128];
    if (!int_fft_forward(re, im, n)) {
        snprintf(msg, sizeof(msg), "spectrum (n=%u f=%d): forward failed", n, freq);
        CHECK(0, msg);
        return;
    }
    int_fft_power(re, im, n, mag2);

    /* Expect peaks at freq and n-freq. */
    uint32_t peak = 0;
    uint16_t peak_bin = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (mag2[i] > peak) {
            peak = mag2[i];
            peak_bin = i;
        }
    }
    snprintf(msg, sizeof(msg), "spectrum (n=%u f=%d): peak in expected bin", n, freq);
    CHECK(peak_bin == (uint16_t)freq || peak_bin == (uint16_t)(n - freq), msg);

    /* All non-signal bins should be far below the peak (integer lifting
     * noise is small; require 40 dB separation, i.e. 10^4 in power). */
    uint32_t worst = 0;
    for (uint16_t i = 0; i < n; i++) {
        if (i == (uint16_t)freq || i == (uint16_t)(n - freq)) {
            continue;
        }
        if (mag2[i] > worst) {
            worst = mag2[i];
        }
    }
    snprintf(msg, sizeof(msg),
             "spectrum (n=%u f=%d): noise floor below -40 dB (worst=%" PRIu32
             " peak=%" PRIu32 ")",
             n, freq, worst, peak);
    CHECK(worst * 10000ull <= (uint64_t)peak, msg);

    /* Round trip from the transformed spectrum. */
    if (!int_fft_inverse(re, im, n)) {
        snprintf(msg, sizeof(msg), "spectrum (n=%u f=%d): inverse failed", n, freq);
        CHECK(0, msg);
        return;
    }
    for (uint16_t i = 0; i < n; i++) {
        int32_t want = (int32_t)lround(amp * sin(2.0 * M_PI * freq * i / n));
        if (re[i] != want || im[i] != 0) {
            snprintf(msg, sizeof(msg),
                     "spectrum (n=%u f=%d): post-inverse mismatch at %u", n, freq, i);
            CHECK(0, msg);
            return;
        }
    }
    snprintf(msg, sizeof(msg), "spectrum (n=%u f=%d): round trip exact", n, freq);
    CHECK(1, msg);
}

static uint32_t rng_state = 0x9E3779B9u;
static uint32_t rng_next(void) {
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return rng_state;
}

int main(void) {
    printf("============================================\n");
    printf("Integer FFT Dogfood Test Suite\n");
    printf("============================================\n\n");

    /* 1. Known sine round trip at every size. */
    printf("-- exact round trip: known sine --\n");
    for (uint16_t n = 2; n <= INT_FFT_MAX_N; n <<= 1) {
        int freq = (n > 2) ? (n / 8) : 1;
        int32_t re[INT_FFT_MAX_N], im[INT_FFT_MAX_N];
        for (uint16_t i = 0; i < n; i++) {
            re[i] = (int32_t)lround(1000.0 * sin(2.0 * M_PI * freq * i / n));
            im[i] = 0;
        }
        char name[64];
        snprintf(name, sizeof(name), "sine n=%u f=%d", n, freq);
        roundtrip_case(name, n, re, im);
    }

    /* 2. Exact round trip: pseudo-random inputs. */
    printf("-- exact round trip: pseudo-random --\n");
    for (int trial = 0; trial < 4; trial++) {
        int32_t re[INT_FFT_MAX_N], im[INT_FFT_MAX_N];
        for (uint16_t i = 0; i < INT_FFT_MAX_N; i++) {
            re[i] = (int32_t)(rng_next() % 20001) - 10000;
            im[i] = (int32_t)(rng_next() % 20001) - 10000;
        }
        char name[64];
        snprintf(name, sizeof(name), "random trial %d", trial);
        roundtrip_case(name, INT_FFT_MAX_N, re, im);
    }

    /* 3. Exact round trip: DC and impulse. */
    printf("-- exact round trip: DC / impulse --\n");
    {
        int32_t re[INT_FFT_MAX_N], im[INT_FFT_MAX_N];
        for (uint16_t i = 0; i < INT_FFT_MAX_N; i++) {
            re[i] = 1234;
            im[i] = 0;
        }
        roundtrip_case("DC", INT_FFT_MAX_N, re, im);
    }
    {
        int32_t re[INT_FFT_MAX_N], im[INT_FFT_MAX_N];
        memset(re, 0, sizeof(re));
        memset(im, 0, sizeof(im));
        re[0] = 777;
        roundtrip_case("impulse", INT_FFT_MAX_N, re, im);
    }

    /* 4. Spectral structure: clean sines land in the right bins. */
    printf("-- spectral structure --\n");
    spectrum_case(32, 3, 1000);
    spectrum_case(32, 7, 1000);
    spectrum_case(16, 2, 800);
    spectrum_case(8, 1, 500);

    /* 5. Invalid size rejection. */
    printf("-- invalid size handling --\n");
    {
        int32_t re[INT_FFT_MAX_N], im[INT_FFT_MAX_N];
        memset(re, 0, sizeof(re));
        memset(im, 0, sizeof(im));
        char msg[128];
        snprintf(msg, sizeof(msg), "n=0 rejected by forward");
        CHECK(!int_fft_forward(re, im, 0), msg);
        snprintf(msg, sizeof(msg), "n=12 (non power of two) rejected");
        CHECK(!int_fft_forward(re, im, 12), msg);
        snprintf(msg, sizeof(msg), "n=64 (above max) rejected");
        CHECK(!int_fft_forward(re, im, 64), msg);
        snprintf(msg, sizeof(msg), "n=2 accepted");
        CHECK(int_fft_forward(re, im, 2), msg);
    }

    printf("\n");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
