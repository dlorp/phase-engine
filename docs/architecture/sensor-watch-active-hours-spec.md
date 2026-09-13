# Sensor Watch: Active Hours & Sleep Tracking Feature Specification

**Version:** 1.0 | **Date:** 2026-02-15 | **Status:** Design

---

## 1. Overview

Two-phase feature adding time-of-day awareness to Sensor Watch firmware:
- **Phase 1 (MVP):** Manual Active Hours setting with behavior changes during sleep window
- **Phase 2:** Automatic sleep detection, quality tracking, smart alarms

---

## 2. Data Structures

### 2.1 Active Hours Configuration (Phase 1)

```c
// Stored in settings (existing settings infrastructure)
typedef struct {
    uint8_t active_start_hour;   // 0-23, default 6
    uint8_t active_start_min;    // 0-59, default 0 (stored as quarter-hours to save bits: 0-3)
    uint8_t active_end_hour;     // 0-23, default 23
    uint8_t active_end_min;      // 0-59, default 0
    uint8_t flags;               // bit 0: enabled, bit 1: sleep_tracking_enabled, bit 2: tap_wake_disabled_in_sleep
} active_hours_config_t;

// Compact bit-packed version for settings register (13 bits total):
//   start_hour: 5 bits (0-23)
//   start_quarter: 2 bits (0-3, representing :00/:15/:30/:45)
//   end_hour: 5 bits (0-23)
//   end_quarter: 2 bits
//   enabled: 1 bit
//   sleep_tracking: 1 bit
//   no_tap_wake: 1 bit
// Total: 17 bits → fits in 3 bytes of settings
```

### 2.2 Sleep Tracking Data (Phase 2)

```c
// Per-night summary — 8 bytes per night, 7 nights = 56 bytes
typedef struct {
    uint8_t date_offset;          // Days since epoch (rolls every 256 days, fine for 7-day window)
    uint8_t sleep_onset_quarter;  // Quarter-hour of detected sleep onset (0-95 = 24h in 15min slots)
    uint8_t wake_quarter;         // Quarter-hour of detected wake
    uint8_t orientation_changes;  // Count of orientation flips (capped at 255)
    uint8_t motion_events;        // Count of significant motion bursts (capped at 255)
    uint8_t restless_quarters;    // Number of 15-min slots with motion > threshold
    uint8_t quality_score;        // 0-100 computed score
    uint8_t flags;                // bit 0: data valid, bit 1: alarm triggered
} sleep_night_t;

// Circular buffer: 7 nights
#define SLEEP_HISTORY_DAYS 7
sleep_night_t sleep_history[SLEEP_HISTORY_DAYS];  // 56 bytes total

// Runtime state (RAM only, not persisted)
typedef struct {
    bool in_sleep_window;         // Currently in sleep hours
    bool sleep_detected;          // Accelerometer confirms likely sleeping
    uint8_t current_orientation;  // Last 6D orientation reading
    uint8_t orientation_change_count; // Tonight's running count
    uint8_t motion_event_count;   // Tonight's running count
    uint8_t quiet_quarters;       // Consecutive 15-min slots with no motion
    uint32_t last_motion_ts;      // Timestamp of last significant motion
} sleep_runtime_t;
```

### 2.3 Storage Plan

| Data | Size | Location | Persistence |
|------|------|----------|-------------|
| Active Hours config | 3 bytes | Settings register | Flash (existing) |
| Sleep history (7 nights) | 56 bytes | EEPROM or reserved flash | Survives reboot |
| Runtime sleep state | ~16 bytes | RAM | Lost on reboot |

**Why 15-minute granularity:** Balances usefulness with storage. Minute-level would need 480 slots/night × multi-byte = too much. 15-min slots give actionable data (sleep onset within 15 min accuracy) at minimal cost.

**Why 7 days:** Enough to show trends, fits in 56 bytes. Circular buffer overwrites oldest night.

---

## 3. Settings UI (Settings Face)

### 3.1 Active Hours Configuration

Add to existing settings face page rotation:

```
Page: "AH"  (Active Hours)
─────────────────────────
Display: "AH 06:00"    ← start time, blinking = editable
ALARM: cycle hours
LIGHT: cycle minutes (quarter-hour steps)
MODE: confirm & move to end time

Display: "AH 23:00"    ← end time
Same controls

Display: "AH on" / "AH OF"  ← enabled toggle
ALARM: toggle
```

### 3.2 Sleep Tracking Toggle (Phase 2)

```
Page: "SL"  (Sleep tracking)
Display: "SL on" / "SL OF"
ALARM: toggle

Page: "St"  (Sleep tap-wake)
Display: "St on" / "St OF"   ← tap-to-wake during sleep
ALARM: toggle
```

### 3.3 Sleep Summary Face (Phase 2)

New watch face: `sleep_summary_face`

```
Default view:     "SL 7h30"     ← last night duration
ALARM press:      "SL  85"      ← quality score (0-100)
ALARM again:      "SL r 12"     ← restless events count
ALARM again:      "on 22:45"    ← sleep onset time
ALARM again:      "up  6:15"    ← wake time
LIGHT:            cycle to previous nights (shows date briefly then duration)
```

---

## 4. Behavior Changes During Sleep Window

### 4.1 Helper Function

```c
// Returns true if current time is OUTSIDE active hours (i.e., sleep time)
bool is_sleep_window(void) {
    if (!settings.active_hours_enabled) return false;
    
    uint8_t now_quarter = current_hour * 4 + (current_min / 15);
    uint8_t start_quarter = settings.active_start_hour * 4 + settings.active_start_quarter;
    uint8_t end_quarter = settings.active_end_hour * 4 + settings.active_end_quarter;
    
    if (start_quarter <= end_quarter) {
        // Normal: active 0600-2300, sleep is outside
        return (now_quarter < start_quarter || now_quarter >= end_quarter);
    } else {
        // Wrapping: active 2200-0600 (night shift)
        return (now_quarter >= end_quarter && now_quarter < start_quarter);
    }
}
```

### 4.2 Motion Threshold (Phase 1 — MVP)

```c
// In cb_accelerometer_wake or motion threshold logic:
uint8_t get_motion_wake_threshold(void) {
    if (is_sleep_window()) {
        return MOTION_THRESHOLD_SLEEP;  // Higher: e.g., 40 (vs normal 16)
    }
    return MOTION_THRESHOLD_NORMAL;     // Default: 16
}
```

**Effect:** During sleep hours, casual arm rolls don't wake the display. Only deliberate wrist raises do.

### 4.3 Display Timeout

```c
uint16_t get_display_timeout_ms(void) {
    if (is_sleep_window()) {
        return 1000;   // 1 second — brief flash if accidentally woken
    }
    return 3000;       // Normal 3 second timeout
}
```

### 4.4 Tap-to-Wake (Optional, Phase 1)

```c
// In accelerometer interrupt handler:
if (is_sleep_window() && settings.no_tap_wake_in_sleep) {
    // Ignore single-tap wake events
    return;
}
```

### 4.5 Alarm Behavior (Phase 2)

No changes to alarm firing in Phase 1 — alarms always fire. In Phase 2, smart alarm can shift wake time within a ±15 min window based on detected sleep phase.

---

## 5. Sleep Detection Algorithm (Phase 2)

### 5.1 State Machine

```
AWAKE → DROWSY → SLEEPING → WAKING → AWAKE

Transitions:
  AWAKE → DROWSY:    in_sleep_window && face_up && no_motion for 5 min
  DROWSY → SLEEPING: no_motion for additional 10 min (15 min total quiet)
  SLEEPING → WAKING: significant motion detected
  WAKING → AWAKE:    motion continues for >2 min OR active_hours_start reached
  WAKING → SLEEPING: motion stops within 2 min (rolled over, went back to sleep)
```

### 5.2 Orientation Tracking

```c
// Called from existing 6D orientation change interrupt
void on_orientation_change(uint8_t new_orientation) {
    sleep_runtime.current_orientation = new_orientation;
    
    if (sleep_runtime.in_sleep_window || sleep_runtime.sleep_detected) {
        sleep_runtime.orientation_change_count++;
        sleep_runtime.last_motion_ts = get_timestamp();
    }
}
```

**Face-up detection:** LIS2DW12 6D detection provides XL/XH/YL/YH/ZL/ZH. Z-High = face up (watch on wrist, arm at side or on chest). This is the primary "lying down" signal.

### 5.3 Quality Score

```c
uint8_t compute_sleep_quality(sleep_night_t *night) {
    uint8_t score = 100;
    
    // Duration factor (ideal: 7-9 hours = 28-36 quarters)
    uint8_t duration = night->wake_quarter - night->sleep_onset_quarter;
    if (duration < 24) score -= (24 - duration) * 2;      // Penalty for < 6h
    if (duration > 40) score -= (duration - 40);           // Mild penalty for > 10h
    
    // Restlessness penalty
    if (night->restless_quarters > 4) score -= (night->restless_quarters - 4) * 3;
    
    // Orientation changes penalty (>20 is very restless)
    if (night->orientation_changes > 10) score -= (night->orientation_changes - 10);
    
    return score > 100 ? 0 : score;  // Clamp
}
```

### 5.4 Smart Alarm (Phase 2+)

```c
// Called every minute during the 30-min window before alarm time
void smart_alarm_check(uint8_t alarm_quarter) {
    uint8_t window_start = alarm_quarter - 2;  // 30 min before (2 quarters)
    uint8_t now_quarter = current_quarter();
    
    if (now_quarter >= window_start && now_quarter < alarm_quarter) {
        // If motion detected in last 2 minutes → light sleep → trigger alarm now
        if (seconds_since(sleep_runtime.last_motion_ts) < 120) {
            trigger_alarm();
        }
    }
    // At exact alarm time, fire regardless
    if (now_quarter == alarm_quarter) {
        trigger_alarm();
    }
}
```

---

## 6. Integration Points

### 6.1 Existing Code Modifications

| File/Function | Change | Phase |
|---|---|---|
| `settings` register | Add 17 bits for active hours config | 1 |
| `cb_accelerometer_wake` | Call `get_motion_wake_threshold()` | 1 |
| Display timeout logic | Call `get_display_timeout_ms()` | 1 |
| Settings face | Add AH pages | 1 |
| `cb_accelerometer_wake` | Add orientation logging call | 2 |
| Alarm face | Add smart alarm option | 2 |
| New: `sleep_summary_face` | New watch face | 2 |

### 6.2 New Files

```
watch-library/shared/watch/
  watch_active_hours.h      // is_sleep_window(), threshold helpers
  watch_active_hours.c

watch-faces/complication/
  sleep_summary_face.h      // Phase 2 (not yet implemented)
  sleep_summary_face.c
```

---

## 7. Power Consumption Estimates

| Component | Current Draw | Notes |
|---|---|---|
| LIS2DW12 (existing) | 45-90 µA | Already running, no change |
| 6D interrupt processing | +0.1 µA avg | ~5-10 orientation changes/night, µs each |
| 15-min timer check | +0.05 µA avg | Brief CPU wake every 15 min during sleep |
| EEPROM write (daily) | Negligible | One 8-byte write per night |
| **Phase 1 total additional** | **< 0.2 µA** | Essentially free — just threshold checks |
| **Phase 2 total additional** | **< 0.5 µA** | Orientation logging + timer |

**Verdict:** Negligible impact. The accelerometer is already the dominant power consumer. Adding time-of-day checks and occasional logging adds virtually nothing.

---

## 8. Implementation Phases

### Phase 1: MVP — Active Hours + Threshold Adjustment
**Effort:** ~2-3 days | **Priority:** High

1. Add active hours config to settings register (3 bytes)
2. Implement `is_sleep_window()` helper
3. Add settings face pages for AH configuration
4. Modify motion wake threshold based on sleep window
5. Modify display timeout during sleep window
6. Optional: tap-to-wake disable toggle

**Deliverable:** Watch behaves differently at night. Less accidental waking. No tracking yet.

### Phase 2: Sleep Detection & Logging
**Effort:** ~3-4 days | **Priority:** Medium

1. Add sleep runtime state machine
2. Hook orientation change logging into accelerometer interrupt
3. Implement 15-minute quarter tracking during sleep window
4. Nightly summary computation and EEPROM storage
5. Quality score algorithm

**Deliverable:** Watch knows when you fell asleep and woke up. Stores 7 nights.

### Phase 3: Sleep Summary Face
**Effort:** ~2 days | **Priority:** Medium

1. New `sleep_summary_face` watch face
2. Display last night stats, cycle through history
3. Quality score display

**Deliverable:** User can review sleep data on the watch.

### Phase 4: Smart Alarms
**Effort:** ~2 days | **Priority:** Low

1. Add light-sleep detection window before alarm
2. Optional smart alarm toggle per alarm
3. ±15 min wake window

**Deliverable:** Wake during light sleep for better mornings.

---

## 9. Testing Plan

### Phase 1 Tests
- **Boundary tests:** Set AH 06:00-23:00, verify `is_sleep_window()` returns correct values at 05:59, 06:00, 22:59, 23:00
- **Wrap-around:** Set AH 22:00-06:00 (night shift), verify logic inverts correctly
- **Threshold:** Verify higher motion threshold applies during sleep window
- **Settings:** Configure AH via settings face, power cycle, verify persistence
- **Display timeout:** Confirm shorter timeout during sleep hours

### Phase 2 Tests
- **Sleep onset:** Place watch face-up with no motion for 15 min during sleep window → verify SLEEPING state
- **Restlessness:** Rotate watch during sleep → verify orientation_change_count increments
- **Wake detection:** Move watch consistently for >2 min → verify transition to AWAKE
- **Brief wake:** Move watch briefly (<2 min) → verify return to SLEEPING
- **Nightly summary:** Verify EEPROM write after wake, check data integrity
- **Circular buffer:** Run for 8+ nights, verify oldest night overwritten

### Phase 3 Tests
- **Face display:** Verify all stats show correctly
- **History navigation:** Cycle through 7 nights with LIGHT button
- **Empty state:** No sleep data → display "no dt" or similar

### Phase 4 Tests
- **Smart alarm window:** Set alarm for 07:00, move wrist at 06:48 → alarm fires early
- **Deep sleep:** No motion in window → alarm fires at exact time
- **Disabled:** Toggle smart alarm off → always fires at exact time

---

## 10. Open Questions

1. **Settings register space:** Need to confirm 17 bits are available in existing settings. If not, may need to extend settings or use separate EEPROM allocation.
2. **Night shift support:** The wrap-around logic handles it, but should we add a "night shift mode" preset?
3. **Multiple sleep periods:** Naps are ignored in this design (only tracks primary sleep in the sleep window). Acceptable for MVP?
4. **Face-up reliability:** Watch position on wrist varies. Need to calibrate what "face-up" means — may need ZH OR ZL depending on which wrist/orientation. Could auto-detect based on first few nights.
5. **Timezone changes:** Active hours are local time — if RTC timezone changes, active hours shift automatically. Correct behavior?
