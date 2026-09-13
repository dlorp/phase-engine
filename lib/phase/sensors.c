/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * ============================================================================
 * BARE-METAL REGISTER MAP — Sensor Hardware on Sensor Watch Pro
 *
 * Datasheet-first reference (cpq/bare-metal-programming-guide approach).
 * These are the actual hardware registers accessed by the sensor pipeline.
 * The LIS2DW driver (lis2dw.c) abstracts I2C transactions, but the register
 * addresses and bit fields below are what the hardware actually sees.
 *
 * LIS2DW12 Accelerometer (I2C address 0x19, SA0=high)
 * ┌─────────┬──────┬───────────────────────────────────────────────────────┐
 * │ Register│ Addr │ Description                                         │
 * ├─────────┼──────┼───────────────────────────────────────────────────────┤
 * │ WHO_AM_I│ 0x0F │ Device ID (reads 0x44)                              │
 * │ CTRL1   │ 0x20 │ ODR[7:4], Mode[3:2], LP_Mode[1:0]                  │
 * │ CTRL2   │ 0x21 │ Boot[7], SoftReset[6], BDU[3], IF_AddInc[2]        │
 * │ CTRL3   │ 0x22 │ SelfTest[7:6], PP_OD[5], LIR[4], SLP_MODE[1:0]    │
 * │ CTRL4   │ 0x23 │ INT1 routing: 6D[7], STap[6], WU[5], FF[4], DT[3] │
 * │ CTRL5   │ 0x24 │ INT2 routing: SlpState[7], SlpChg[6], Boot[5]      │
 * │ CTRL6   │ 0x25 │ BW[7:6], Range[5:4], FDS[3], LowNoise[2]          │
 * │ STATUS  │ 0x27 │ FIFO_THS[7], WU_IA[6], SlpState[5], DRDY[0]       │
 * │ OUT_XYZ │0x28- │ 16-bit axis data (X_L/H, Y_L/H, Z_L/H)            │
 * │         │ 0x2D │   In LP mode 1: 12-bit (left-aligned in 16-bit)    │
 * │ WU_THS  │ 0x34 │ SleepOn[7], DoubleTap[6], Threshold[5:0]           │
 * │ WU_DUR  │ 0x35 │ Stationary[4] — sleep detection threshold           │
 * │ WU_SRC  │ 0x38 │ FF_IA[5], SlpState[4], WU_IA[3], X/Y/Z_WU[2:0]   │
 * └─────────┴──────┴───────────────────────────────────────────────────────┘
 *
 * Configuration used by Phase Engine (sensors_configure_accel):
 *   CTRL1: ODR=1.6Hz (low power), Mode=LowPower, LP_Mode=1 (12-bit)
 *   CTRL6: Range=±2g, LowNoise=off (power optimization)
 *   WU_THS: Threshold=1 (minimum wakeup sensitivity)
 *   WU_DUR: Stationary=1 (sleep detection enabled)
 *   CTRL4: INT1_WU routed (wakeup interrupt on INT1 pin)
 *
 * SAM L22 ADC (for light sensor on Pro board, A2 pin)
 * ┌─────────┬──────┬───────────────────────────────────────────────────────┐
 * │ Register│ Addr │ Description                                         │
 * ├─────────┼──────┼───────────────────────────────────────────────────────┤
 * │ CTRLA   │0x4200│ REFSEL[5:4], DIFFMODE[3], FREERUN[2], ENABLE[1]    │
 * │ INPUTCTRL│0x420│ MUXPOS[11:4], MUXNEG[3:0]                          │
 * │ SWTRIG  │0x420│ START[1] — software trigger for conversion           │
 * │ RESULT  │0x422│ 16-bit conversion result (right or left-aligned)     │
 * │ INTFLAG │0x420│ RESRDY[0] — conversion complete flag                 │
 * └─────────┴──────┴───────────────────────────────────────────────────────┘
 *
 * The Movement API (watch_enable_adc, watch_get_analog_pin_level) wraps
 * these registers. The bare-metal approach would access them directly:
 *   ADC->CTRLA.reg = ADC_CTRLA_ENABLE;
 *   ADC->INPUTCTRL.reg = ADC_INPUTCTRL_MUXPOS_A2;
 *   ADC->SWTRIG.reg = ADC_SWTRIG_START;
 *   while (!ADC->INTFLAG.bit.RESRDY);
 *   uint16_t raw = ADC->RESULT.reg;
 *
 * For Phase Engine, the Movement API is retained for compatibility with
 * the Sensor Watch ecosystem. The register map above documents what
 * happens underneath — the "datasheet-first" understanding.
 * ============================================================================
 */

#ifdef PHASE_ENGINE_ENABLED

#include "sensors.h"
#include "rls_filter.h"
#include "lis2dw.h"
#include "watch.h"
#include "movement.h"
#include <string.h>
#include <stdlib.h>

static uint16_t _compute_variance(const uint16_t *buffer, uint8_t count);
static uint16_t _abs16(int16_t val);

void sensors_init(struct sensor_state_t *state, bool has_accel) {
    memset(state, 0, sizeof(struct sensor_state_t));
    state->has_accelerometer = has_accel;
    state->lux_sensor_healthy = true;  // Assume healthy until proven otherwise
    
    // Initialize adaptive EMA filters
    // Lux: moderate adaptation range (indoor/outdoor transitions can be dramatic)
    // alpha_min=0.0625 (16-sample TC), alpha_max=0.5 (2-sample TC), error_scale=100
    aema_init_custom(&state->lux_aema, 2048, 16384, 100);
    
    // Motion: wider adaptation range (rest→sudden motion is a big jump)
    // alpha_min=0.03125 (32-sample TC), alpha_max=0.5 (2-sample TC), error_scale=200
    aema_init_custom(&state->motion_aema, 1024, 16384, 200);
    
    // Note: Thermistor initialization is handled by Movement
    
    state->initialized = true;
}

void sensors_configure_accel(struct sensor_state_t *state) {
    if (!state || !state->initialized || !state->has_accelerometer) {
        return;
    }
    
    /*
     * LIS2DW12 accelerometer configuration for Phase Engine sleep detection.
     *
     * Bare-metal register sequence (datasheet-first, cpq approach):
     *   I2C write to 0x19:
     *     CTRL1 (0x20) = ODR[7:4]=0001 (1.6Hz LP) | Mode[3:2]=00 (LP) | LPMODE[1:0]=00 (12-bit)
     *     CTRL6 (0x25) = Range[5:4]=00 (±2g) | LowNoise[2]=0 (off, power save)
     *     WU_THS (0x34) = Threshold[5:0]=000001 (minimum wakeup sensitivity)
     *     WU_DUR (0x35) = Stationary[4]=1 (enable sleep detection)
     *     CTRL4 (0x23) = INT1_WU[5]=1 (route wakeup to INT1 pin)
     *
     * Power budget: ~1.6µA in LP mode @ 1.6Hz with 12-bit resolution.
     * The LIS2DW's built-in wakeup engine detects motion without CPU intervention.
     * INT1 pin triggers the SAM L22's EIC (External Interrupt Controller),
     * which can wake the CPU from standby (the "bare-metal sleep" pattern).
     *
     * Movement API wrappers below perform the same I2C register writes.
     * Each lis2dw_set_*() call translates to: watch_i2c_write8(LIS2DW_ADDRESS, reg, val)
     */
    lis2dw_set_mode(LIS2DW_MODE_LOW_POWER);              // CTRL1[3:2] = 00
    lis2dw_set_low_power_mode(LIS2DW_LP_MODE_1);          // CTRL1[1:0] = 00 (12-bit)
    lis2dw_set_data_rate(LIS2DW_DATA_RATE_LOWEST);        // CTRL1[7:4] = 0001 (1.6Hz)
    lis2dw_set_low_noise_mode(false);                      // CTRL6[2] = 0 (power save)
    lis2dw_set_range(LIS2DW_RANGE_2_G);                    // CTRL6[5:4] = 00 (±2g)
    lis2dw_configure_wakeup_threshold(1);                  // WU_THS[5:0] = 000001
    lis2dw_enable_sleep();                                 // WU_DUR[4] = 1 (stationary detect)
    lis2dw_enable_stationary_motion_detection();           // WU_DUR[4] + WU_THS config
    lis2dw_configure_int1(LIS2DW_CTRL4_INT1_WU);          // CTRL4[5] = 1 (INT1 wakeup)
}

void sensors_update(struct sensor_state_t *state) {
    if (!state || !state->initialized) {
        return;
    }
    
    // PR #65: Update motion tracking
    if (!state->has_accelerometer) {
        state->motion_active = false;
        state->motion_variance = 0;
        state->motion_intensity = 0;
        state->motion_magnitude = 0;
    } else {
        uint8_t wake_src = lis2dw_get_wakeup_source();
        bool is_awake = (wake_src & LIS2DW_WAKEUP_SRC_WAKEUP) != 0;
        bool is_sleeping = (wake_src & LIS2DW_WAKEUP_SRC_SLEEP_STATE) != 0;
        
        if (is_awake) {
            state->motion_active = true;
            state->inactivity_minutes = 0;
            
            // Phase 4E: Count movement interrupt for sleep tracking
            if (state->epoch_movement_count < 255) {
                state->epoch_movement_count++;
            }
            if (state->hourly_movement_count < 255) {
                state->hourly_movement_count++;
            }
        } else if (is_sleeping) {
            state->inactivity_minutes += 15;
            if (state->inactivity_minutes >= SENSOR_INACTIVITY_MIN) {
                state->motion_active = false;
            }
        }
        
        lis2dw_reading_t raw = lis2dw_get_raw_reading();
        uint16_t mag = _abs16(raw.x) + _abs16(raw.y) + _abs16(raw.z);
        
        state->motion_buffer[state->motion_buf_idx] = mag;
        state->motion_buf_idx = (state->motion_buf_idx + 1) % SENSOR_MOTION_BUFFER_SIZE;
        if (state->motion_buf_count < SENSOR_MOTION_BUFFER_SIZE) {
            state->motion_buf_count++;
        }
        
        state->motion_magnitude = mag;
        state->motion_variance = _compute_variance(state->motion_buffer, state->motion_buf_count);
        // Adaptive EMA: tracks fast motion events while smoothing rest noise.
        // Replaces fixed EMA (alpha=0.25) that couldn't distinguish between
        // noise floor jitter and real movement transitions.
        state->motion_intensity = (uint16_t)aema_update(&state->motion_aema, (int16_t)mag);
    }
    
    // PR #66: Update lux + temperature
    sensors_sample_lux(state);
    sensors_sample_temperature(state);
}

uint16_t sensors_get_motion_variance(const struct sensor_state_t *state) {
    return state ? state->motion_variance : 0;
}

uint16_t sensors_get_motion_intensity(const struct sensor_state_t *state) {
    return state ? state->motion_intensity : 0;
}

bool sensors_is_motion_active(const struct sensor_state_t *state) {
    return state ? state->motion_active : false;
}

static uint16_t _compute_variance(const uint16_t *buffer, uint8_t count) {
    if (count < 2) return 0;
    
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++) {
        sum += buffer[i];
    }
    uint16_t mean = (uint16_t)(sum / count);
    
    uint32_t sq_sum = 0;
    for (uint8_t i = 0; i < count; i++) {
        int32_t diff = (int32_t)buffer[i] - (int32_t)mean;
        sq_sum += (uint32_t)(diff * diff);
    }
    
    uint32_t var = sq_sum / count;
    return (var > UINT16_MAX) ? UINT16_MAX : (uint16_t)var;
}

static uint16_t _abs16(int16_t val) {
    return (val < 0) ? (uint16_t)(-val) : (uint16_t)val;
}

// ============================================================================
// PR #66: Lux + Temperature Integration
// ============================================================================

void sensors_sample_lux(struct sensor_state_t *state) {
    if (!state || !state->initialized) {
        return;
    }
    
#if HAS_LIGHT_SENSOR
    // Pro board: sample ADC, apply EMA filter
    watch_enable_adc();
    
    // Read ambient light from A2 pin (light sensor on Pro board)
    uint16_t raw = watch_get_analog_pin_level(HAL_GPIO_A2_pin());
    
    watch_disable_adc();
    
    // Convert raw ADC to approximate lux
    // Raw 0-65535 → roughly 0-10000 lux (calibration TBD during dogfooding)
    // Simple linear mapping: lux = raw / 6 (gives ~0-10922 range)
    uint16_t lux = raw / 6;
    
    // Store raw sample for stuck detection
    state->lux_raw = lux;
    
    // Stuck sensor detection: if raw reading is identical for N consecutive
    // samples, the sensor or ADC is likely frozen. Mark unhealthy.
    // Compare against the AEMA's current output (not a separate cached field).
    uint16_t lux_filtered = (uint16_t)aema_get(&state->lux_aema);
    if (lux_filtered == lux) {
        if (state->lux_stuck_count < LUX_STUCK_THRESHOLD) {
            state->lux_stuck_count++;
        }
        if (state->lux_stuck_count >= LUX_STUCK_THRESHOLD) {
            state->lux_sensor_healthy = false;
        }
    } else {
        state->lux_stuck_count = 0;
        state->lux_sensor_healthy = true;
    }
    
    // EMA filter: now adaptive (replaces fixed alpha=0.25)
    // The AEMA adapts based on prediction error:
    //   - Stable indoor light → alpha drops to 0.0625 (heavy smoothing, 16-sample TC)
    //   - Indoor/outdoor transition → alpha rises to 0.5 (fast tracking, 2-sample TC)
    // This is strictly better than the fixed EMA: same smoothing during stable
    // periods, but much faster response when the user walks outside.
    //
    // Stability: The AEMA's alpha is clamped to [alpha_min, alpha_max] ⊂ (0, 1),
    // so the filter is Lyapunov-stable regardless of adaptation. The adaptation
    // only moves the pole within this pre-verified stable range.
    aema_update(&state->lux_aema, (int16_t)lux);
#else
    // Non-Pro boards: no light sensor — AEMA stays at 0 (initialized state)
    state->lux_raw = 0;
    state->lux_sensor_healthy = false;
#endif
}

void sensors_sample_temperature(struct sensor_state_t *state) {
    if (!state || !state->initialized) {
        return;
    }
    
    // Use existing Movement API (handles thermistor + LIS2DW fallback)
    // movement_get_temperature() checks movement_state.has_thermistor and
    // falls back to LIS2DW12 internal temp sensor if no thermistor available
    float temp_c = movement_get_temperature();
    
    // Convert to 0.1°C units (e.g., 20.5°C → 205)
    if (temp_c == (float)0xFFFFFFFF) {
        // No temperature sensor available, use reasonable fallback
        state->temperature_c10 = 200;  // 20.0°C (room temperature)
    } else {
        // Convert float to 0.1°C units with rounding
        // Use int16_t to support negative temperatures (e.g., -30°C → -300)
        state->temperature_c10 = (int16_t)((temp_c * 10.0f) + (temp_c >= 0 ? 0.5f : -0.5f));
    }
}

uint16_t sensors_get_lux(const struct sensor_state_t *state) {
    if (!state || !state->lux_sensor_healthy) {
        return 0;
    }
    return (uint16_t)aema_get(&state->lux_aema);
}

bool sensors_is_lux_healthy(const struct sensor_state_t *state) {
    return state ? state->lux_sensor_healthy : false;
}

int16_t sensors_get_temperature_c10(const struct sensor_state_t *state) {
    return state ? state->temperature_c10 : 0;
}

// ============================================================================
// Phase 4E: Sleep Tracking Helpers
// ============================================================================

void sensors_tick_epoch(struct sensor_state_t *state) {
    if (!state || !state->initialized) {
        return;
    }
    
    state->epoch_seconds++;
    
    // Check if light is currently detected (lux > threshold)
    // Threshold: 10 lux (dim light detection)
    uint16_t current_lux = (uint16_t)aema_get(&state->lux_aema);
    if (current_lux > 10 && state->hourly_light_minutes < 60) {
        state->hourly_light_minutes++;
    }
}

uint8_t sensors_get_epoch_movement_count(const struct sensor_state_t *state) {
    return state ? state->epoch_movement_count : 0;
}

void sensors_reset_hourly_counters(struct sensor_state_t *state) {
    if (!state || !state->initialized) {
        return;
    }
    
    state->hourly_light_minutes = 0;
    state->hourly_movement_count = 0;
}

uint8_t sensors_get_hourly_light_minutes(const struct sensor_state_t *state) {
    return state ? state->hourly_light_minutes : 0;
}

uint8_t sensors_get_hourly_movement_count(const struct sensor_state_t *state) {
    return state ? state->hourly_movement_count : 0;
}

#endif // PHASE_ENGINE_ENABLED
