# Phase Engine: Adaptive EMA + RLS Filters

**Status:** AEMA implemented and live; RLS implemented, tested, **not wired into the sensor pipeline** (see Status below) · **Date:** 2026-07-05 (original), fixed 2026-09-12
**Vault source:** `signal-processing/2026-07-01-rls-adaptive-filters-dsp-chapter` (RLS theory); `embedded-systems/2026-06-03-bare-metal-programming-guide` (datasheet-first register documentation)

## What

Two adaptive filter implementations for the SAM L22 (no FPU, 32KB RAM,
Cortex-M0+), replacing/complementing the sensor pipeline's fixed-alpha EMA:

- `lib/phase/rls_filter.h` — API: `aema_init`, `aema_init_custom`,
  `aema_update`, `aema_get`, `aema_get_alpha`; `rls_init`, `rls_init_custom`,
  `rls_update`, `rls_get`, `rls_get_error`
- `lib/phase/rls_filter.c` — implementations
- `tests/test_rls_filter.c` / `.sh` — host dogfood harness (added 2026-09-12;
  neither filter had test coverage before)

## Algorithm

**Adaptive EMA (AEMA)** — replaces the old fixed-pole EMA (alpha=0.25) in
`sensors.c` for lux and motion-intensity filtering:

```
alpha = alpha_min + (alpha_max - alpha_min) * |input - ema| / error_scale
ema   = ema + (alpha * (input - ema)) >> 15
```

Alpha is bounded to `[alpha_min, alpha_max]` (default 0.0625–0.5) by
construction: each update is a convex combination, so the output can never
leave the range spanned by past inputs regardless of how alpha varies —
stable by construction, no separate proof needed.

**RLS filter (diagonal-P approximation)** — a 2nd-order adaptive predictor,
O(N) per sample instead of O(N²) for full RLS:

```
k[i]    = p[i]*x[i] / (lambda*delta + sum_j p[j]*x[j]^2)   // gain
w[i]   += k[i] * (input - sum_j w[j]*x[j])                  // weight update
p[i]    = (p[i] - k[i]*p[i]*x[i]) / lambda * gamma           // P update
```

The P-update was originally implemented as `k[i]^2 * x[i]^2`, which drops
the `p[i]` dependence entirely and does not reduce to the recursion above.
Fixed 2026-09-12 to `(k[i] * k_num) >> 15`, where `k_num = p[i]*x[i]` is
already computed for the gain — descaling `k[i]`'s own Q15 factor gives the
exact correction with no extra approximation (derivable directly from the
gain formula: `k[i]*denom ≈ k_num << 15`). The bug's practical effect: fed a
learnable ramp signal, the pre-fix filter's prediction error got *worse*
over 400 iterations (205→349) instead of better — this is what "drops the
p[i] dependence" looks like in practice, not just an abstract concern. A
separate `px2[]` overflow (declared `int32_t`, but reachable to ~2^40 at
`p[i]`'s clamp ceiling) was widened to `int64_t` in the same fix.

## Stability — heuristic, not proven

AEMA is stable by construction (see above). **RLS is not.** The leaky
factor (gamma < 1) and a hard clamp on `p[i]` (max 2^25, reset to 1.0 if it
ever goes negative) bound the damage, but do not constitute a stability
proof. The vault source cited above lists fixed-point RLS numerical
stability as an open, unanswered question, and this implementation doesn't
resolve it — the P-update fix corrects a formula bug, it doesn't prove the
corrected recursion stays well-behaved indefinitely. Literature on
adaptive-filter "windup" (covariance inflation during flat/low-excitation
stretches, e.g. overnight near-constant temperature) suggests the current
leaky+clamp approach is an adequate minimum against divergence but not a
complete answer — see `PHASE_ENGINE_REWORK.md` §1.3 for a concrete,
literature-motivated hardening (excitation-gated forgetting + periodic P
reset) that hasn't been implemented yet.

## Status: RLS is not wired into the live sensor pipeline — by design

This is a deliberate gate, not an oversight:
- The vault source's own open question on fixed-point RLS stability is
  inherited here, not resolved.
- The P-update bug above is a concrete instance of that gap — it took
  actually running the filter on a signal to surface it, not inspection.
- The new test suite checks boundedness and basic convergence over a few
  hundred to 500 iterations of synthetic signals — real coverage, but it
  says nothing about weeks of continuous on-device operation.
- There is no live consumer yet. AEMA already covers the actual current
  requirement (lux/motion smoothing) and is the only behavioral change to
  the running pipeline.

Ship the primitive plus its first real test coverage now; gate live wiring
on either a concrete consumer whose own validation matches its actual use,
or the deeper numerical-stability hardening referenced above.

## Verification

Host (macOS, gcc), `bash tests/test_rls_filter.sh`:
- AEMA: alpha stays within `[alpha_min, alpha_max]` on every iteration of a
  step-response test; converges to within 9 units of a step target after
  200 samples (the shift-based correction `(alpha*error)>>15` has a real,
  documented convergence floor around error < 16 at `alpha_min` — this is
  inherent to the fixed-point design, not a bug).
- RLS: `p[i]` stays within `[0, 2^25]` on every iteration across a
  near-stationary signal (final `p = [32768, 32768]`, mean \|err\| over the
  last 50 samples = 1.76) and a large-amplitude alternating-sign signal
  stressing the fixed-point arithmetic near the int16 range (bounds held
  throughout, same clamp ceiling never exceeded).
- RLS prediction error trends down on a learnable ramp: mean \|err\| over
  the first 50 samples = 175.74, over the last 50 = 2.92 — confirms the
  corrected filter still functions as an adaptive predictor, not just
  "doesn't overflow." Verified this suite fails against the pre-fix P-update
  formula on this same test (error rises 205→349 instead of falling).

Target (arm-none-eabi-gcc, `-mcpu=cortex-m0plus -mthumb -Os`): zero
warnings; full firmware build (`make BOARD=sensorwatch_pro DISPLAY=classic
PHASE_ENGINE_ENABLED=1`) links clean.

## Flash / RAM Cost

Standalone module (`arm-none-eabi-size`, `-Os`, M0+):

| Section | Bytes |
|---|---|
| .text (both filters) | 808 |
| .data / .bss | 0 |
| **Module flash total** | **808 B** |

Per-instance state (measured via `sizeof`, matches ARM alignment — no
host/target divergence for these field types):

| Struct | Size |
|---|---|
| `aema_state_t` | 16 B |
| `rls_state_t` (`RLS_ORDER=2`) | 36 B |

`sensors.c` currently instantiates 2 `aema_state_t` (lux, motion) = 32 B
live RAM. `rls_state_t` costs nothing today since it's not instantiated
anywhere outside its own test.

## Constraints honored

- No external DSP libs, no floats, no dynamic allocation.
- AEMA change is a drop-in replacement for the prior fixed-alpha EMA —
  same call sites in `sensors.c`, same steady-state smoothing behavior,
  faster response during transitions.
- RLS addition touches no existing call path — `rls_filter.c` compiles
  into every `PHASE_ENGINE_ENABLED` build (Makefile SRCS) but is otherwise
  inert until a future caller exists.
