# Tap-to-Wake Feature

## Overview
Tap-to-wake enables instant display wake by tapping the watch crystal. This feature uses the LIS2DW12 accelerometer's dedicated tap detection hardware on Sensor Watch Pro boards.

## How It Works

### Hardware Constraints
The LIS2DW12 accelerometer has a critical limitation:
- **Tap detection interrupt** can ONLY route to INT1 (pin A3)
- **Pin A3 is NOT an RTC wake pin** - cannot wake from deep sleep
- **Pin A4 (INT2) IS an RTC wake pin** - can wake from deep sleep

This means pure hardware tap detection cannot wake the device from sleep mode.

### Hybrid Solution: Motion Wake + Tap Polling
To work around this limitation, we use a **hybrid approach**:

1. **Motion detection** is configured on INT2 (pin A4) - this CAN wake from sleep
2. When ANY motion occurs (including taps), the device wakes via the A4 interrupt
3. **Software immediately polls** the tap registers to determine if it was a tap
4. If tap bits are set, the event is processed as a tap; otherwise just a motion wake

**Trade-off:** Both tap and wrist-raise motion can wake the device. We cannot distinguish at the hardware level whether the wake was tap-only or motion-only. Software checks the tap register after waking to determine the cause.

### Hardware
- **Accelerometer:** LIS2DW12 (Pro board only)
- **Detection:** Hardware-based tap recognition on Z-axis
- **Wake interrupt:** Motion detection on INT2 (pin A4) - RTC wake capable
- **Tap polling:** Software reads tap status from INT1 source register after wake
- **Sampling rate:** 400Hz during tap detection
- **Modes:** Single tap and double tap both supported

### Software
When motion (including tap) is detected:
1. Accelerometer generates interrupt on INT2 (A4) - device wakes from sleep
2. `cb_accelerometer_wake()` callback executes immediately
3. Software polls `lis2dw_get_interrupt_source()` to read tap status
4. If SINGLE_TAP or DOUBLE_TAP bits are set, `has_pending_accelerometer` flag is raised
5. Main loop calls `_movement_get_accelerometer_events()`
6. Tap events (SINGLE_TAP or DOUBLE_TAP) are generated if tap was detected
7. `_movement_reset_inactivity_countdown()` wakes display
8. Events are passed to current watch face (can be handled by faces if desired)

## Power Consumption

**Active tap detection:**
- Accelerometer: ~45-90µA (400Hz sampling, LP Mode 1, low noise OFF)
- Always-on (tap detection enabled at boot)
- Previous versions incorrectly configured for ~400µA (high-performance or low-noise mode)

**Optimizations applied:**
- Low-Power Mode 1 (12-bit resolution) set BEFORE ramping to 400Hz
- Low-noise mode disabled (adds power consumption)
- Mode configuration order ensures LP mode is not overridden

**Trade-off:**
- Modest baseline power increase (~50-90µA) vs motion-only detection
- Instant response (<100ms) vs wrist-raise delay (~500ms)
- Crystal tap = intuitive UX gesture

## Configuration

### Tap Sensitivity
Configured in `movement_enable_tap_detection_if_available()`:
```c
// Z-axis threshold: 12 (range 0-31)
lis2dw_configure_tap_threshold(0, 0, 12, LIS2DW_REG_TAP_THS_Z_Z_AXIS_ENABLE);

// Tap duration: shock(2), quiet(2), latency(2)
lis2dw_configure_tap_duration(2, 2, 2);
```

**Tuning:**
- Increase threshold (12 → 15+) for firmer taps required
- Decrease threshold (12 → 8-10) for more sensitive detection
- Adjust durations to distinguish tap from sustained pressure

### Disabling Tap Detection
Watch faces can disable tap detection to save power:
```c
movement_disable_tap_detection_if_available();
```

Re-enable when returning to normal mode:
```c
movement_enable_tap_detection_if_available();
```

## Board Support

### Sensor Watch Pro
✅ **Full support** - Has LIS2DW12 accelerometer
- Crystal tap → instant wake
- Both single and double tap detected
- ~400µA additional power draw

### Sensor Watch Green
⚠️ **No accelerometer** - Tap detection not available
- `movement_enable_tap_detection_if_available()` returns false
- Falls back to button-only wake
- No additional power consumption

## User Experience

### Tap-to-Wake Flow
```
User taps crystal
    ↓
Motion detection triggers A4 wake interrupt
    ↓
Software polls tap registers
    ↓
~100-200ms response time (slightly slower than pure hardware interrupt)
    ↓
Display wakes
    ↓
Current face visible
    ↓
Inactivity timer reset (won't sleep immediately)
```

**Note:** Response time is slightly slower than a pure hardware tap interrupt would be, because we rely on motion detection to wake the device, then poll the tap registers. However, the user experience is still excellent - the delay is barely perceptible.

### Coexistence with Other Features
Tap detection works alongside:
- **Motion detection** (INT2/A4) - Required for tap-to-wake (see hybrid approach above)
  - Both tap and wrist-raise can wake the device
  - Software distinguishes tap from motion after wake
- **Button wake** - Always available as fallback
- **Step counting** - Can share accelerometer (different data rate/FIFO)

### Integration with Watch Faces
Watch faces receive tap events and can:
- Ignore taps (default - just wake display)
- Handle EVENT_SINGLE_TAP for actions
- Handle EVENT_DOUBLE_TAP for different actions
- See `ping_face.c`, `countdown_face.c`, etc. for examples

## Implementation Details

### Initialization (movement.c)
```c
// During app_init(), after accelerometer basic setup:
lis2dw_enable_interrupts();
movement_enable_tap_detection_if_available(); // ← NEW
```

### Event Generation (_movement_get_accelerometer_events)
```c
uint8_t int_src = lis2dw_get_interrupt_source();

if (int_src & LIS2DW_REG_ALL_INT_SRC_DOUBLE_TAP) {
    accelerometer_events |= 1 << EVENT_DOUBLE_TAP;
    _movement_reset_inactivity_countdown(); // ← Wake display
}

if (int_src & LIS2DW_REG_ALL_INT_SRC_SINGLE_TAP) {
    accelerometer_events |= 1 << EVENT_SINGLE_TAP;
    _movement_reset_inactivity_countdown(); // ← Wake display
}
```

## Testing

### Emulator
The web emulator doesn't simulate tap detection (no accelerometer model). Test on actual hardware.

### Hardware Testing
1. Flash `firmware-sensorwatch_pro-classic-tap-to-wake.uf2` to Pro board
2. Let display sleep (wait for inactivity timeout)
3. Tap crystal firmly with fingertip
4. Display should wake immediately

**Troubleshooting:**
- Too sensitive? → Increase threshold in `lis2dw_configure_tap_threshold()`
- Not sensitive enough? → Decrease threshold or check Z-axis orientation
- No response? → Verify INT1 (A3) connection, check with printf debugging

## Future Enhancements

Potential additions:
- [ ] Configurable tap sensitivity via settings face
- [ ] Single vs double tap preference (disable one to save cycles)
- [ ] Tap-only mode (disable motion detection, rely on tap + buttons)
- [ ] Tap gesture recognition (double-tap → navigate, triple-tap → light)
- [ ] Sleep mode tap disable (reduce power when sleeping overnight)

## References

- **LIS2DW12 Datasheet:** https://www.st.com/en/mems-and-sensors/lis2dw12.html
- **Application Note AN5038:** Recommendations for 2g range, tap detection config
- **movement.h:** Event type definitions (EVENT_SINGLE_TAP, EVENT_DOUBLE_TAP)
- **Step counter PR #9:** Shows FIFO usage and motion detection coexistence
