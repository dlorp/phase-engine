# Lesson: Extend, Not Replace

**Date:** 2026-02-20
**Context:** Phase 4A sensor integration (PR #65)

## What We Did Wrong

We directly called `thermistor_driver_*()` in `sensors.c`, bypassing the existing `movement_get_temperature()` API.

## Why It's Wrong

1. Movement already handles:
   - Checking `movement_state.has_thermistor`
   - Falling back to LIS2DW12 internal temp sensor
   - Returning 0xFFFFFFFF on error

2. By reimplementing this logic:
   - We duplicated code
   - We may call drivers on boards without sensors
   - We violated the "extend, not replace" principle

## What We Should Do

**Before adding new sensor code:**
1. Check if Movement already has an API for this
2. Read existing sensor faces to see how they do it
3. Use existing abstractions when possible

**Example:**
- ❌ WRONG: Call `thermistor_driver_get_temperature()` directly
- ✅ RIGHT: Call `movement_get_temperature()` (already handles everything)

## Other Movement APIs to Use

- `movement_get_temperature()` - Temperature (thermistor + LIS2DW fallback)
- `movement_use_imperial_units()` - User preferences

## Accelerometer Note

Unlike temperature, the LIS2DW12 accelerometer is accessed directly via `lis2dw_*()` functions in this codebase. Existing watch faces (e.g., `lis2dw_monitor_face.c`) use these low-level APIs directly, so our usage in `sensors.c` is consistent with established patterns.

## Principle

**The watch has existing sensor abstractions. USE them, don't reimplement them.**
