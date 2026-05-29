# second-movement Session Notes

## 2026-05-28 [~15:00 AKDT] — Shared circadian LUT, chronotype offset, solarpunk scores, WK median filter

**Status:** Complete
**Agent:** hyph4
**Type:** feature
**Commits:** 1 this session

### Executive Summary
Implemented four Phase Engine improvements: extracted duplicated cosine LUT to a shared header (fixing a phase inconsistency where three subsystems diverged from the documented 2 PM circadian peak), added compile-time chronotype offset for web builder integration, defined solarpunk score label constants (Sol/Dew/Sap/Hum), and replaced the WK baseline simple average with a median-of-3 filter to prevent outlier days from dominating the 7-day baseline.

### Work Completed
- **Shared circadian LUT:** Created `lib/phase/circadian_lut.h` with true cos(2π*(h-14)/24)*1000 values. Phase engine, metric_em, and metric_energy now share a single LUT. Eliminated 96 bytes of duplicated flash and fixed an undocumented phase inconsistency — the old phase_engine LUT peaked at hour 20 (8 PM), not hour 14 (2 PM) as the comment claimed.
- **Chronotype offset:** Added `PHASE_CHRONOTYPE_OFFSET` compile-time define (default 0, range -4 to +4). Applied to all three LUT lookup sites. The web builder computes offset from active hours midpoint and injects it via CFLAGS. Zero RAM cost.
- **Solarpunk score constants:** Created `lib/phase/scores.h` with `SCORE_LABEL_SOL`/`DEW`/`SAP`/`HUM` macros. All four pass the segment LCD position 1 character constraint. Includes documentation of metric-to-score mapping.
- **WK baseline median filter:** Replaced 7-day simple average in `sleep_data.c` with median-of-middle-3 (bubble sort on stack, drop 2 highest and 2 lowest). Falls back to simple average when fewer than 5 valid days exist. Prevents a single hiking day from depressing the baseline for 6 days.

### Files Modified
- `lib/phase/circadian_lut.h` — new: shared 24-entry cosine LUT with correct phase anchors
- `lib/phase/scores.h` — new: Sol/Dew/Sap/Hum score label constants
- `lib/phase/phase_engine.c` — replace local LUT with shared include + chronotype offset lookup
- `lib/phase/sleep_data.c` — replace simple average WK baseline with median-of-3 filter
- `lib/metrics/metric_em.c` — replace local LUT (and its negation) with shared include + chronotype offset
- `lib/metrics/metric_energy.c` — replace local LUT with shared include + chronotype offset

### Build Verification
- `make BOARD=sensorwatch_blue DISPLAY=classic PHASE_ENGINE_ENABLED=1` — clean compile
- Size: 153,716 text + 2,812 data + 7,028 bss = 163,556 bytes (well within 256KB flash)

### Notes & Observations
- The three old LUTs had different phase anchors: phase_engine peaked at hour 8 (value -1000, used with `1000+curve`), metric_em peaked at hour 10 (negated), metric_energy peaked at hour 10. The shared LUT unifies all three on hour 14 (2 PM), which matches documented human circadian alertness peak.
- Stash applied from `main` required deduplication — `is_active_hours` and `phase_detect_anomalies` existed on both the stash source and the target branch.
- SESSION_NOTES.md created as new file (repo had no prior session notes).

### Next Steps
- Bangle.js 2 calibration logger script (separate session)
- Web builder: add PHASE_CHRONOTYPE_OFFSET computation from active hours midpoint
- Update circadian_score_face.c to use SCORE_LABEL_* constants
