/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Integer-to-Integer FFT (lifting scheme) — implementation.
 *
 * See int_fft.h for design rationale and the vault reference.
 *
 * Structure: radix-2 decimation-in-time, bit-reversed input. Each stage's
 * twiddle rotation is a 3-step lifting shear using Q14 coefficients from
 * int_fft_lut.h; every multiply is truncated with a right shift, identical
 * in forward and inverse, which makes the transform exactly reversible.
 *
 * Forward butterfly (a' = a + W*b, b' = a - W*b):
 *   tr,ti = lift_rotate(re[b], im[b], tw)
 *   re[b] = re[a] - tr;  im[b] = im[a] - ti;
 *   re[a] += tr;         im[a] += ti;
 *
 * Inverse butterfly (a = (a' + b')/2, b = W^-1 * ((a' - b')/2)):
 *   ar = (re[a] + re[b]) >> 1;  ai = (im[a] + im[b]) >> 1;
 *   tr = (re[a] - re[b]) >> 1;  ti = (im[a] - im[b]) >> 1;
 *   br,bi = lift_irotate(tr, ti, tw)
 *   re[a] = ar; im[a] = ai; re[b] = br; im[b] = bi;
 *
 * The >> 1 halving in the inverse is exact: forward butterfly sums and
 * differences are always even (2a and 2Wb respectively), so no precision
 * is lost anywhere in the round trip.
 */

#include "int_fft.h"
#include "int_fft_lut.h"

#ifdef PHASE_ENGINE_ENABLED

#define INT_FFT_Q_SHIFT 14 /* Q14 coefficients: 1.0 == 16384 */

/* Lifting shear rounding: (q14 * v) >> 14 with arithmetic right shift.
 * Identical in forward and inverse => exact reconstruction regardless of
 * coefficient quantization (vault Finding 1: any rounding mode works). */
static inline int32_t int_fft_round(int16_t q14, int32_t v) {
    return (int32_t)(((int64_t)q14 * v) >> INT_FFT_Q_SHIFT);
}

/* Forward rotation by twiddle t: (x,y) -> W_t * (x,y) via 3 lifting shears. */
static void int_fft_lift_rotate(int32_t *x, int32_t *y,
                                const int_fft_twiddle_t *tw) {
    int32_t a = *x;
    int32_t b = *y;

    /* L1: x += q1*y */
    a += int_fft_round(tw->q1, b);
    /* L2: y += q2*x */
    b += int_fft_round(tw->q2, a);
    /* L3: x += q3*y */
    a += int_fft_round(tw->q3, b);

    /* Type B carries an extra -1 on the whole rotation. */
    if (tw->negate) {
        a = -a;
        b = -b;
    }

    *x = a;
    *y = b;
}

/* Inverse rotation: (x,y) -> W_t^-1 * (x,y). Same coefficients, reversed
 * order, subtraction. Exact inverse of int_fft_lift_rotate. */
static void int_fft_lift_irotate(int32_t *x, int32_t *y,
                                 const int_fft_twiddle_t *tw) {
    int32_t a = *x;
    int32_t b = *y;

    /* Type B: -1 factor first (commutes with the rotation). */
    if (tw->negate) {
        a = -a;
        b = -b;
    }

    /* L3^-1: x -= q3*y */
    a -= int_fft_round(tw->q3, b);
    /* L2^-1: y -= q2*x */
    b -= int_fft_round(tw->q2, a);
    /* L1^-1: x -= q1*y */
    a -= int_fft_round(tw->q1, b);

    *x = a;
    *y = b;
}

static bool int_fft_valid_size(uint16_t n) {
    if (n < 2 || n > INT_FFT_MAX_N) {
        return false;
    }
    /* Power of two. */
    return (n & (n - 1)) == 0;
}

/* In-place bit reversal (DIT requires bit-reversed input order). */
static void int_fft_bitrev(int32_t *re, int32_t *im, uint16_t n) {
    uint16_t j = 0;
    for (uint16_t i = 1; i < n; i++) {
        uint16_t bit = n >> 1;
        while (j & bit) {
            j ^= bit;
            bit >>= 1;
        }
        j ^= bit;
        if (i < j) {
            int32_t tr = re[i];
            int32_t ti = im[i];
            re[i] = re[j];
            im[i] = im[j];
            re[j] = tr;
            im[j] = ti;
        }
    }
}

bool int_fft_forward(int32_t *re, int32_t *im, uint16_t n) {
    if (!int_fft_valid_size(n)) {
        return false;
    }

    int_fft_bitrev(re, im, n);

    for (uint16_t span = 2; span <= n; span <<= 1) {
        uint16_t half = span >> 1;
        for (uint16_t m = 0; m < n; m += span) {
            for (uint16_t j = 0; j < half; j++) {
                /* LUT index: j * (INT_FFT_MAX_N / span). With INT_FFT_MAX_N
                 * a power of two this is j * (INT_FFT_MAX_N >> log2(span)). */
                uint16_t lut_idx = j * (INT_FFT_MAX_N / span);
                const int_fft_twiddle_t *tw = &int_fft_lut[lut_idx];

                uint16_t a_idx = m + j;
                uint16_t b_idx = a_idx + half;

                int32_t tr = re[b_idx];
                int32_t ti = im[b_idx];
                int_fft_lift_rotate(&tr, &ti, tw);

                int32_t ar = re[a_idx];
                int32_t ai = im[a_idx];

                re[b_idx] = ar - tr;
                im[b_idx] = ai - ti;
                re[a_idx] = ar + tr;
                im[a_idx] = ai + ti;
            }
        }
    }
    return true;
}

bool int_fft_inverse(int32_t *re, int32_t *im, uint16_t n) {
    if (!int_fft_valid_size(n)) {
        return false;
    }

    for (uint16_t span = n; span >= 2; span >>= 1) {
        uint16_t half = span >> 1;
        for (uint16_t m = 0; m < n; m += span) {
            for (uint16_t j = 0; j < half; j++) {
                uint16_t lut_idx = j * (INT_FFT_MAX_N / span);
                const int_fft_twiddle_t *tw = &int_fft_lut[lut_idx];

                uint16_t a_idx = m + j;
                uint16_t b_idx = a_idx + half;

                /* Halving is exact (sums/diffs are even). */
                int32_t ar = (re[a_idx] + re[b_idx]) >> 1;
                int32_t ai = (im[a_idx] + im[b_idx]) >> 1;
                int32_t tr = (re[a_idx] - re[b_idx]) >> 1;
                int32_t ti = (im[a_idx] - im[b_idx]) >> 1;

                int_fft_lift_irotate(&tr, &ti, tw);

                re[a_idx] = ar;
                im[a_idx] = ai;
                re[b_idx] = tr;
                im[b_idx] = ti;
            }
        }
    }

    /* DIT with bit-reversed input: the inverse transform ends with the
     * same permutation (bit reversal is an involution). */
    int_fft_bitrev(re, im, n);

    return true;
}

void int_fft_power(const int32_t *re, const int32_t *im, uint16_t n,
                   uint32_t *mag2) {
    for (uint16_t i = 0; i < n; i++) {
        int64_t r = re[i];
        int64_t m = im[i];
        int64_t p = r * r + m * m;
        if (p > UINT32_MAX) {
            mag2[i] = UINT32_MAX;
        } else {
            mag2[i] = (uint32_t)p;
        }
    }
}

#endif /* PHASE_ENGINE_ENABLED */
