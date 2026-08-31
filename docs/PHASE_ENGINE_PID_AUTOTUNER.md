# Phase Engine Notebook: Data-Driven PID Auto-Tuning

**Date:** 2026-08-30
**Source vault entry:** `control-theory/2026-07-16-mipt-ml-pid-tuning-ru.md` (MIPT paper: ML methods learn PID coefficients from system response data; automatic tuning replaces manual Ziegler-Nichols)
**Task:** Phase Engine improvement only. Keep existing Lyapunov/circadian control work intact.

## What was added

`lib/phase/pid_tuner.c` + `lib/phase/pid_tuner.h` — a data-driven PID auto-tuner
for Cortex-M0+ (no FPU, integer math, Q8/Q15 fixed point).

The tuner learns Kp/Ki/Kd from step-response data instead of manual tuning:

1. **Step test.** The caller applies a step to the plant and feeds
   `(t_ms, y)` samples via `pid_tuner_add_sample()`.
2. **FOPDT identification** (First-Order-Plus-Dead-Time model
   `K * e^(-Ls) / (Ts + 1)`), all integer math:
   - K = steady-state output change / input step (Q15)
   - L = dead time: first crossing of a 3% noise band
   - T = time constant: averaged over the 28.3% / 63.2% / 86.5% / 95%
     response fractions, so the whole curve informs the fit.
3. **Tuning rules:**
   - **Primary (PID_TUNE_OK): IMC (lambda) tuning** — used when the curve has
     settled (terminal least-squares slope <= 10% of the initial slope).
     Gains: `Kp = (T + L/2)/(K*(λ + L/2))`, `Ti = T + L/2`, `Td = T*L/(2T+L)`,
     with λ = 0.25*T.
   - **Fallback (PID_TUNE_ZN_FALLBACK): Ziegler-Nichols open-loop** — used when
     response data is insufficient for a full fit (curve still rising: slope
     ratio > 10%) but T and L are estimable. Gains:
     `Kp = 1.2*T/(K*L)`, `Ti = 2L`, `Td = 0.5L`.
   - **PID_TUNE_INSUFFICIENT** — too few samples, < 2% response magnitude, or no
     reliable T/L. Caller keeps collecting; gains untouched.

Also includes `pid_controller_t` — a fixed-point PID (Q8 gains, conditional
integration / anti-windup) that consumes the tuned gains and closes a real loop.

## Stability

- IMC gains are BIBO-stable for FOPDT plants for any λ > 0 (Rivera, Morari,
  Skogestad 1986).
- The fit is bounded: degenerate fits (no crossing, zero/near-zero gain) return
  PID_TUNE_INSUFFICIENT rather than unsafe gains.
- The PID controller clamps output and uses conditional integration, so an
  untuned or noisy loop cannot run away.
- The settled test is slope-based (scale-free), which avoids the classic pitfall
  of comparing against an estimated steady state that is biased low while the
  curve is still rising.

## Dogfood results (host simulation, `tests/test_pid_tuner.c`)

FOPDT plant simulator, step test -> tune -> closed loop. Outputs in sensor scale
(0-1000), control input 0-5000, dt = 50ms.

### Scenario 1: full response -> data-driven IMC
Plant: K=0.20, T=1.0s, L=0.2s. 64 samples @ 50ms = 3.2s (~3T past dead time).

```
Tune status : IMC (data-driven)
Fitted model: K=0.187 (true 0.200)  T=762ms (true 1000)  L=200ms (true 200)
Tuned gains : Kp=15.895  Ki=0.015625/ms  Kd=1398.719*ms
Closed loop: settle=1600ms  overshoot=0.1%  final err=0.00
```

Outcome: dead time identified exactly; K within 6.5%; T within 24% (integer
quantization of samples). Closed loop tracks setpoint with zero steady-state
error, negligible overshoot, settling at ~1.6s for a T=1s plant.

### Scenario 2: partial response -> Ziegler-Nichols fallback
Plant: K=0.20, T=2.0s, L=0.3s. 64 samples @ 50ms = 3.2s (~1.5T past dead time,
~76% settled -> still rising -> ZN fallback).

```
Tune status : Ziegler-Nichols fallback
Fitted model: K=0.146 (true 0.200)  T=1032ms (true 2000)  L=300ms (true 300)
Tuned gains : Kp=28.273  Ki=0.046875/ms  Kd=4241.016*ms
Closed loop: settle=2350ms  overshoot=5.1%  final err=0.00
```

Outcome: dead time exact, ZN gains close the loop with 5.1% overshoot and zero
steady-state error despite the partial-curve T estimate being low.

### Scenario 3: insufficient data (4 samples < minimum 8)
Correctly returns `PID_TUNE_INSUFFICIENT` with gains zeroed.

```
0 scenario(s) failed.
```

## Run the dogfood

```bash
bash tests/test_pid_tuner.sh
```

## Build verification

- `gcc -DPHASE_ENGINE_ENABLED -Ilib/phase -Wall -Wextra -Werror` — clean.
- `arm-none-eabi-gcc -mcpu=cortex-m0plus -mthumb -Os -Wall -Wextra -Werror` — clean.
- Flash cost of the tuner: ~2.1 KB code, 0 BSS (state is caller-instantiated:
  64 samples x 6 bytes = 384 bytes only when a caller uses auto-tuning).
- Full firmware build: `make BOARD=sensorwatch_pro DISPLAY=classic PHASE_ENGINE_ENABLED=1`.

## Files

- `lib/phase/pid_tuner.h` — API, state structs, tuning formulas (new)
- `lib/phase/pid_tuner.c` — implementation (new)
- `tests/test_pid_tuner.c` — host dogfood harness (new)
- `tests/test_pid_tuner.sh` — test runner (new)
- `Makefile` — added `pid_tuner.c` to SRCS (modified)

## Notes

- Not wired to a specific watch face yet; it is a reusable Phase Engine library
  routine. A future control loop (e.g., light/exposure actuation or a
  calibration servo) can call `pid_tuner_begin/add_sample/tune` during a
  calibration step and use `pid_controller_update` for closed-loop regulation.
- The Lyapunov/circadian control work (saturating-arithmetic phase score,
  circadian LUT, anomaly detection) is untouched.
