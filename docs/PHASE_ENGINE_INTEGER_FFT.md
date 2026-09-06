# Phase Engine: Integer-to-Integer FFT (Lifting Scheme)

**Status:** Implemented (host + target verified) · **Date:** 2026-09-06
**Vault source:** `signal-processing/2026-05-31-integer-fft-lifting-scheme` (qiita fukuroder, scarcity=3)

## What

A reversible integer FFT for on-device spectral analysis on Sensor Watch
class MCUs (Cortex-M0+, no FPU). New files:

- `lib/phase/int_fft.h` — API: `int_fft_forward`, `int_fft_inverse`,
  `int_fft_power`
- `lib/phase/int_fft.c` — radix-2 DIT implementation, lifting-scheme twiddles
- `lib/phase/int_fft_lut.h` — generated Q14 twiddle LUT (16 entries, 128B flash)
- `tools/gen_int_fft_table.py` — LUT generator (regeneration only; firmware
  build needs no Python)
- `tests/test_integer_fft.c` / `.sh` — dogfood harness

## Algorithm

Every twiddle rotation `W_t = [[c,-s],[s,c]]` is decomposed into three
lifting-scheme shears (Oraintara, Chen, Nguyen 2002):

- **Type A** (cos t >= 0): `W = [[1,(c-1)/s],[0,1]] x [[1,0],[s,1]] x [[1,(c-1)/s],[0,1]]`
- **Type B** (cos t < 0): `W = -[[1,(c+1)/s],[0,1]] x [[1,0],[-s,1]] x [[1,(c+1)/s],[0,1]]`

Each shear is a multiply-add truncated by a right shift (`(q14 * v) >> 14`).
The **same coefficients and the same truncation** run in forward and inverse,
so the transform is structurally reversible: forward + inverse reproduces the
input bit for bit, no matter the coefficient quantization (vault Finding 1:
any rounding mode works).

Forward is unscaled (DC amplitude A lands in bin 0 as `A * n`); the inverse
halves per stage, which is exact because forward butterfly sums/differences
are always even. Round trip is the identity.

## Tradeoff (deliberate)

| Sacrificed | Kept |
|---|---|
| Exact DFT: rounding per lifting step breaks linearity (`T(a+b) != T(a)+T(b)`) and adds small per-bin noise | Exact integer reconstruction — no float error accumulation anywhere |
| Float twiddles / trig at runtime | Q14 LUT, right-shift truncation, pure integer math |

For on-device spectral analysis (find the dominant bin of a motion/light
signal), dominant-peak correctness is what matters, and the noise floor is
measured below -40 dB relative to signal peak in the test harness.

## Verification

Host (macOS, gcc):
- 27 checks, 0 failures. Exact round trip proven for known sine inputs at
  every size (n = 2..32), 4 pseudo-random trials, DC, impulse; spectral
  peak placement for f = 1..7; invalid-size rejection.

Target (arm-none-eabi-gcc, `-mcpu=cortex-m0plus -mthumb`):
- `-Wall -Wextra -Os`: zero warnings.
- Full firmware build (`make BOARD=sensorwatch_blue DISPLAY=classic
  PHASE_ENGINE_ENABLED=1`): passes.

## Flash / RAM Cost

Standalone module (`arm-none-eabi-size`, `-Os`, M0+):

| Section | Bytes |
|---|---|
| .text | 764 |
| .rodata (LUT) | 128 |
| .data | 0 |
| .bss | 0 |
| **Total flash** | **892 B** |
| **Total RAM** | **0 B static** (buffers are caller-provided: 2 × n × 4 bytes, 256 B for n=32) |

Whole-firmware delta with `int_fft.c` in SRCS: **0 B** until a caller exists
— gossamer links with `--gc-sections` and drops the unreferenced module. The
cost when a spectral-analysis face starts calling it is the 892 B above.

## Usage sketch

```c
#include "int_fft.h"

int32_t re[32], im[32];      // 256 B total, caller-owned
uint32_t mag2[32];

for (uint16_t i = 0; i < 32; i++) {
    re[i] = raw_sensor_sample(i);   // int16 widened
    im[i] = 0;
}
int_fft_forward(re, im, 32);
int_fft_power(re, im, 32, mag2);    // find peak bin, no sqrt needed
// int_fft_inverse(re, im, 32) recovers the original samples exactly.
```

## Constraints honored

- No external DSP libs (stdlib only).
- Fits Sensor Watch class MCU: 892 B flash, 0 static RAM.
- Existing clock/circadian logic untouched (no changes to movement.c,
  phase_engine.c, sensors.c, or any existing watch face).
