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

#ifdef PHASE_ENGINE_ENABLED

#include "metric_energy.h"
#include "circadian_lut.h"

// Chronotype offset: shifts circadian peak from default 2 PM (hour 14)
// Set by web builder from active hours midpoint. Range: -4 to +4.
#ifndef PHASE_CHRONOTYPE_OFFSET
#define PHASE_CHRONOTYPE_OFFSET 0
#endif

// Activity bonus scaling factor (activity / 50 → max 20)
#define ENERGY_ACTIVITY_DIVISOR 50
#define ENERGY_MAX_BONUS 20

uint8_t metric_energy_compute(uint16_t phase_score, uint8_t sd_score, uint16_t recent_activity,
                              uint8_t hour, bool has_accelerometer) {
    // Base formula: phase_score - (sd_score / 3)
    int16_t energy = (int16_t)phase_score - (sd_score / 3);
    
    if (has_accelerometer) {
        // Normal mode: Add activity bonus
        // Bonus = min(20, recent_activity / 50)
        uint8_t activity_bonus = (uint8_t)(recent_activity / ENERGY_ACTIVITY_DIVISOR);
        if (activity_bonus > ENERGY_MAX_BONUS) {
            activity_bonus = ENERGY_MAX_BONUS;
        }
        energy += activity_bonus;
        
    } else {
        // Fallback mode: Add circadian bonus (no accelerometer)
        // Bonus = (circadian_lut[hour] + 1000) / 100  →  range [0, 20]
        // circadian_lut_24 peaks at hour 14 (2 PM), trough at hour 2 (2 AM)
        // Chronotype offset shifts peak to user's actual rhythm
        if (hour >= 24) hour = 23;  // Safety clamp
        uint8_t chrono_hour = (hour + 24 - PHASE_CHRONOTYPE_OFFSET) % 24;
        int16_t circ_raw = circadian_lut_24[chrono_hour];
        uint8_t circ_bonus = (uint8_t)((circ_raw + 1000) / 100);  // Map [-1000, +1000] → [0, 20]
        energy += circ_bonus;
    }
    
    // Clamp to valid range [0, 100]
    if (energy < 0) energy = 0;
    if (energy > 100) energy = 100;
    
    return (uint8_t)energy;
}

#endif // PHASE_ENGINE_ENABLED
