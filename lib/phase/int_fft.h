/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Integer-to-Integer FFT (lifting scheme)
 *
 * A reversible integer FFT for on-device spectral analysis on Cortex-M0+
 * (Sensor Watch class: no FPU, 32KB RAM). Every twiddle rotation is
 * decomposed into three lifting-scheme shears (Oraintara, Chen, Nguyen
 * 2002; applied from vault signal-processing/2026-05-31-integer-fft-lifting-scheme)
 * and truncated with a right shift — the identical truncation runs in both
 * the forward and inverse directions, so the transform is structurally
 * reversible: forward + inverse reproduces the input exactly, bit for bit.
 *
 * Tradeoff (deliberate):
 *   - Exact integer reconstruction, no float accumulation anywhere.
 *   - The output is NOT exactly the DFT: rounding inside each lifting step
 *     breaks linearity (T(a+b) != T(a) + T(b) in general) and adds small
 *     per-bin noise. Bins are still dominant-peak correct for clean tones
 *     (see tests/test_integer_fft.c), which is what on-device spectral
 *     analysis needs. No scaling is applied in the forward pass; the
 *     inverse pass halves per stage (exact: butterfly sums are always
 *     even), so the round trip is the identity.
 *
 * Size: max N = INT_FFT_MAX_N (default 32, power of two). RAM = 2 * n * 4
 * bytes caller-provided (256 bytes for n=32), 0 B static. Flash = LUT
 * (16 entries x 8 bytes = 128 B .rodata, padded for alignment -- not 6 B/
 * entry) + ~760 B .text at -Os (~892 B total; see
 * docs/PHASE_ENGINE_INTEGER_FFT.md for the measured build).
 *
 * References:
 *   - Oraintara, Chen, Nguyen (2002), "Integer fast Fourier transform",
 *     IEEE Trans. Signal Processing
 *   - fukuroder, Qiita "integer FFT by lifting scheme" (JP)
 *   - vault: signal-processing/2026-05-31-integer-fft-lifting-scheme
 */

#ifndef INT_FFT_H_
#define INT_FFT_H_

#include <stdint.h>
#include <stdbool.h>

#ifdef PHASE_ENGINE_ENABLED

/* Maximum transform size (power of two). Smaller n is supported at
 * runtime; the LUT covers all twiddles up to this size. */
#define INT_FFT_MAX_N 32

/**
 * In-place forward radix-2 integer FFT (DIT, bit-reversed input).
 *
 * Unscaled: a DC input of amplitude A lands in bin 0 with value A * n.
 * For n=32 with int16 inputs (|x| <= 32767), worst-case intermediates
 * stay below ~2^20, far inside int32. Products use int64 intermediates
 * (single SMULL on Cortex-M0+).
 *
 * @param re Real part, n entries (in-place)
 * @param im Imaginary part, n entries (in-place)
 * @param n  Size, power of two in [2, INT_FFT_MAX_N]
 * @return   true on success, false if n is invalid
 */
bool int_fft_forward(int32_t *re, int32_t *im, uint16_t n);

/**
 * In-place inverse integer FFT — exact inverse of int_fft_forward().
 *
 * Applies the same lifting coefficients in reverse order with the same
 * right-shift truncation, then halves per stage (each halving is exact
 * because forward butterfly sums/differences are always even). Result:
 * int_fft_inverse(int_fft_forward(x)) == x exactly.
 *
 * @param re Real part, n entries (in-place)
 * @param im Imaginary part, n entries (in-place)
 * @param n  Size, power of two in [2, INT_FFT_MAX_N]
 * @return   true on success, false if n is invalid
 */
bool int_fft_inverse(int32_t *re, int32_t *im, uint16_t n);

/**
 * Magnitude-squared power spectrum (for peak detection; sqrt is
 * expensive on M0+, compare mag2 values instead).
 *
 * @param re   Real part from int_fft_forward()
 * @param im   Imaginary part from int_fft_forward()
 * @param n    Transform size
 * @param mag2 Output array, n entries (uint32, saturating)
 */
void int_fft_power(const int32_t *re, const int32_t *im, uint16_t n,
                   uint32_t *mag2);

#endif /* PHASE_ENGINE_ENABLED */
#endif /* INT_FFT_H_ */
