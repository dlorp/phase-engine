# Playlist Controller

**Phase 3 Component:** Weighted Zone-Based Face Rotation System

The Playlist Controller manages **adaptive face rotation** by computing zone-specific metric relevance and dynamically building a prioritized "playlist" of watch faces. It bridges the **Metrics Engine** (biological state) and the **Zone Faces** (adaptive UI).

---

## Overview

### Purpose

Instead of manually scrolling through all watch faces, the playlist controller:
1. **Detects your current phase zone** (Emergence, Momentum, Active, Descent)
2. **Ranks metrics by relevance** for that zone
3. **Builds a playlist** of 3-6 faces showing the most important metrics
4. **Auto-rotates faces** based on dwell time (user can also advance manually)

**Example:** In **Momentum zone** (26-50% phase score), WK and Energy metrics are most relevant, so those faces appear first in rotation.

---

## Architecture

```
┌──────────────────┐
│  Phase Score     │  0-100 from phase_compute()
│  (Circadian)     │
└────────┬─────────┘
         │
         v
┌──────────────────┐
│  Zone Detection  │  EMERGENCE → MOMENTUM → ACTIVE → DESCENT
│  (with hysteresis)│
└────────┬─────────┘
         │
         v
┌──────────────────┐      ┌─────────────────┐
│ Metric Weighting │<─────│ Metrics Snapshot│
│  (zone-specific) │      │  SD/EM/WK/Eng/Com│
└────────┬─────────┘      └─────────────────┘
         │
         v
┌──────────────────┐
│  Playlist Build  │  Sort by weight × value
│  (top 6 metrics) │
└────────┬─────────┘
         │
         v
┌──────────────────┐
│  Face Rotation   │  Manual (ALARM) or Auto (dwell timeout)
└──────────────────┘
```

---

## API Reference

### Initialization

```c
#include "playlist.h"

playlist_state_t state;
memset(&state, 0, sizeof(state));
playlist_init(&state);
```

### Update (Every 15 Minutes or on State Change)

```c
void playlist_update(playlist_state_t *state, 
                     uint16_t phase_score,              // 0-100
                     const metrics_snapshot_t *metrics); // SD/EM/WK/Energy/Comfort
```

**Triggers:**
- Every 15 minutes (when metrics update)
- After manual face advance
- On zone transition

**Behavior:**
- Recomputes zone from phase score
- Applies zone-specific weights to metrics
- Sorts metrics by `weight × value`
- Rebuilds face_indices[] array (top 6)

### Get Current Face

```c
uint8_t metric_index = playlist_get_current_face(&state);
// Returns: 0=SD, 1=EM, 2=WK, 3=Energy, 4=Comfort
```

### Manual Advance (ALARM Button)

```c
void playlist_advance(playlist_state_t *state);
```

**Behavior:**
- Moves to next face in rotation (wraps at end)
- Resets dwell timer
- Plays button feedback sound (if enabled)

### Get Current Zone

```c
phase_zone_t zone = playlist_get_zone(&state);
// Returns: ZONE_EMERGENCE, ZONE_MOMENTUM, ZONE_ACTIVE, or ZONE_DESCENT
```

---

## Zone System

### Four Phases of the Circadian Cycle

| Zone | Phase Score | Time of Day (typical) | Focus | Top Metrics |
|------|-------------|----------------------|-------|-------------|
| **Emergence** | 0-25 | 4 AM - 9 AM | Waking, orienting | SD, EM, Comfort |
| **Momentum** | 26-50 | 9 AM - 2 PM | Building energy | WK, Energy, SD |
| **Active** | 51-75 | 2 PM - 8 PM | Peak output | Energy, EM, Comfort |
| **Descent** | 76-100 | 8 PM - 4 AM | Winding down | Comfort, SD, EM |

### Zone-Specific Metric Weights

```c
// ZONE_EMERGENCE (0-25): Just woke up, need status check
static const uint8_t WEIGHTS_EMERGENCE[5] = {
    100,  // SD:      Critical (did I sleep enough?)
    80,   // EM:      Important (how do I feel?)
    40,   // WK:      Low (too early to care)
    60,   // Energy:  Moderate (building up)
    90    // Comfort: High (environment matters)
};

// ZONE_MOMENTUM (26-50): Building the day
static const uint8_t WEIGHTS_MOMENTUM[5] = {
    70,   // SD:      Moderate (yesterday's sleep still matters)
    60,   // EM:      Moderate (mood stabilizing)
    100,  // WK:      Critical (am I fully alert yet?)
    90,   // Energy:  High (capacity check)
    50    // Comfort: Low (less sensitive during ramp-up)
};

// ZONE_ACTIVE (51-75): Peak productivity
static const uint8_t WEIGHTS_ACTIVE[5] = {
    40,   // SD:      Low (not thinking about last night)
    80,   // EM:      High (mood affects performance)
    60,   // WK:      Moderate (already awake)
    100,  // Energy:  Critical (need to know capacity)
    70    // Comfort: Moderate (optimize environment)
};

// ZONE_DESCENT (76-100): Winding down
static const uint8_t WEIGHTS_DESCENT[5] = {
    90,   // SD:      High (will I sleep well tonight?)
    70,   // EM:      Moderate (managing evening mood)
    30,   // WK:      Low (irrelevant now)
    50,   // Energy:  Low (letting go of output)
    100   // Comfort: Critical (optimize for sleep prep)
};
```

---

## Playlist Algorithm

### Step 1: Compute Weighted Relevance

For each metric `i`:
```
relevance[i] = weight[zone][i] × metric_value[i] / 100
```

**Example (Momentum zone):**
```
Metrics:     SD=60,  EM=70,  WK=40,  Energy=55,  Comfort=80
Weights:     70,     60,     100,    90,         50
Relevance:   42,     42,     40,     49.5,       40
Sorted:      Energy(49.5) → SD(42) → EM(42) → WK(40) → Comfort(40)
```

### Step 2: Sort by Relevance (Bubble Sort)

```c
// In-place sort of metric indices by relevance (descending)
for (i = 0; i < count-1; i++) {
    for (j = 0; j < count-i-1; j++) {
        if (relevance[j] < relevance[j+1]) {
            swap(face_indices[j], face_indices[j+1]);
        }
    }
}
```

### Step 3: Build Playlist

- **Top 6 metrics** → `face_indices[]` array
- **Face count** → number of non-zero relevance metrics (typically 5, since JL is stubbed)

### Step 4: Hysteresis (Zone Transition)

To prevent flickering at zone boundaries:
- New zone must persist for **2 consecutive updates** (30 minutes)
- Only then does the playlist rebuild

```c
if (new_zone != current_zone) {
    if (new_zone == pending_zone) {
        consecutive_count++;
        if (consecutive_count >= 2) {
            current_zone = new_zone;  // Commit transition
            rebuild_playlist();
        }
    } else {
        pending_zone = new_zone;
        consecutive_count = 1;
    }
}
```

---

## Auto-Advance (Dwell Timer)

**Status:** Not yet implemented (deferred to Phase 4)

**Planned Behavior:**
- Each face has a `dwell_limit` (e.g., 5 minutes)
- After `dwell_limit` ticks, auto-advance to next face
- User interaction resets dwell timer
- Can be disabled in settings

**Current Behavior:** Manual advance only (ALARM button)

---

## Integration Example

### In `movement.c` (Phase 3 PR 6)

```c
#ifdef PHASE_ENGINE_ENABLED
// In _movement_handle_top_of_minute() (every 15 min):
if (tick_count >= 15) {
    tick_count = 0;
    
    // Update metrics
    metrics_update(&movement_state.metrics, ...);
    
    // Get current metrics
    metrics_snapshot_t snapshot;
    metrics_get(&movement_state.metrics, &snapshot);
    
    // Update playlist
    uint16_t phase_score = phase_compute(...);  // From Phase 1-2
    playlist_update(&movement_state.playlist, phase_score, &snapshot);
    
    // If playlist mode active, switch to recommended face
    if (movement_state.playlist_mode_active) {
        uint8_t metric_idx = playlist_get_current_face(&movement_state.playlist);
        phase_zone_t zone = playlist_get_zone(&movement_state.playlist);
        uint8_t face_idx = get_zone_face_index(metric_idx, zone);
        movement_move_to_face(face_idx);
    }
}
#endif
```

### ALARM Button Handler

```c
#ifdef PHASE_ENGINE_ENABLED
case EVENT_ALARM_BUTTON_UP:
    if (movement_state.playlist_mode_active) {
        playlist_advance(&movement_state.playlist);
        
        uint8_t metric_idx = playlist_get_current_face(&movement_state.playlist);
        phase_zone_t zone = playlist_get_zone(&movement_state.playlist);
        uint8_t face_idx = get_zone_face_index(metric_idx, zone);
        movement_move_to_face(face_idx);
    }
    break;
#endif
```

---

## Resource Usage

### Flash: ~1.5 KB

| Component | Size |
|-----------|------|
| `playlist.c` | ~1,200 B |
| Weight tables | ~200 B |
| Sorting logic | ~100 B |
| **Total** | **~1,500 B** |

### RAM: 24 Bytes (playlist_state_t)

```c
typedef struct {
    uint8_t zone;                  // 1 B: Current zone (0-3)
    uint8_t face_count;            // 1 B: Faces in rotation (0-6)
    uint8_t face_indices[6];       // 6 B: Sorted metric indices
    uint8_t current_face;          // 1 B: Index into face_indices
    uint16_t dwell_ticks;          // 2 B: Time on current face
    uint16_t dwell_limit;          // 2 B: Auto-advance threshold
    uint8_t pending_zone;          // 1 B: Hysteresis state
    uint8_t consecutive_count;     // 1 B: Hysteresis counter
    // Padding: ~9 B
} playlist_state_t;  // 24 B total
```

### BKUP: 0 Bytes

Playlist state is **runtime-only** (no persistence needed).

---

## Testing

### Unit Tests

```bash
# Test zone detection
make test_playlist_zone

# Test metric weighting
make test_playlist_weights

# Test face sorting
make test_playlist_sort

# Test hysteresis
make test_playlist_hysteresis
```

### Integration Test

```bash
# Full playlist controller test
make test_playlist_integration
```

---

## Configuration

### Compile-Time Flags

```makefile
PHASE_ENGINE_ENABLED = 1   # Enable playlist controller
```

### Runtime Configuration (Future)

- [ ] Dwell timeout (auto-advance interval)
- [ ] Custom zone boundaries (adjust phase score thresholds)
- [ ] Metric exclusions (hide specific metrics from rotation)

---

## Known Limitations

1. **Auto-advance not implemented:** Requires dwell timer integration
2. **Zone face dispatch stubbed:** Returns face 0 (clock) until zone faces merged
3. **No user preferences:** All users see same weights (Phase 4: personalization)
4. **No A/B testing:** Weight tuning is manual (Phase 4: adaptive learning)

---

## Future Enhancements (Phase 4+)

- [ ] Auto-advance based on dwell timer
- [ ] User-configurable weights (e.g., "I care more about Comfort than EM")
- [ ] Adaptive weight learning (observe which faces user spends time on)
- [ ] Zone boundary customization (early bird vs night owl)
- [ ] Metric exclusion (hide JL if never traveling)
- [ ] Multi-metric faces (e.g., "Emergence Summary" showing SD+EM+Comfort)

---

## Troubleshooting

### Playlist not updating?

- Check that `PHASE_ENGINE_ENABLED=1` in build
- Verify `metrics_update()` is called every 15 minutes
- Ensure `phase_score` is non-zero (stubbed to 50 if phase engine unavailable)

### Zone transitions too fast?

- Hysteresis requires **2 consecutive updates** (30 min total)
- If still flickering, increase `HYSTERESIS_THRESHOLD` in `playlist.c`

### Face rotation stuck?

- Check `playlist_mode_active` flag (must be true)
- Verify `get_zone_face_index()` returns valid face indices
- Ensure ALARM button events reach `playlist_advance()`

---

## Related Phase Engine Modules

This file documents the Playlist Controller specifically. Other `lib/phase/`
modules have their own dedicated docs:

- `pid_tuner.c/.h` — data-driven PID auto-tuner, see
  `docs/PHASE_ENGINE_PID_AUTOTUNER.md`
- `int_fft.c/.h` — reversible integer FFT for spectral analysis, see
  `docs/PHASE_ENGINE_INTEGER_FFT.md`
- `rls_filter.c/.h` — adaptive EMA (live) + RLS filter (not yet wired in),
  see `docs/PHASE_ENGINE_RLS_AEMA_FILTER.md`

## References

- `PHASE3_IMPLEMENTATION_PLAN.md` — Overall Phase 3 architecture
- `lib/metrics/README.md` — Metrics Engine (data source for playlist)
- `PHASE3_BUDGET_REPORT.md` — Resource usage details

---

**Questions? See `docs/PHASE3_FAQ.md` or ask in #second-movement**
