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

#include "metric_em.h"
#include <stdlib.h>  // for abs()
#include "circadian_lut.h"

// Chronotype offset: shifts circadian peak from default 2 PM (hour 14)
// Set by web builder from active hours midpoint. Range: -4 to +4.
#ifndef PHASE_CHRONOTYPE_OFFSET
#define PHASE_CHRONOTYPE_OFFSET 0
#endif

// Lunar component (20%): 29-day cycle approximation

uint8_t metric_em_compute(uint8_t hour, uint16_t day_of_year, uint16_t activity_variance) {
    // Clamp hour to valid range (0-23)
    if (hour >= 24) hour = 23;
    
    // Circadian component (40%)
    // circadian_lut_24 ranges from -1000 to +1000, peaks at hour 14 (2 PM)
    // Chronotype offset shifts peak to user's actual rhythm
    uint8_t chrono_hour = (hour + 24 - PHASE_CHRONOTYPE_OFFSET) % 24;
    int16_t circ_raw = circadian_lut_24[chrono_hour];
    uint8_t circ_score = (uint8_t)((circ_raw + 1000) / 20);  // Map [-1000, +1000] → [0, 100]
    
    // Lunar component (20%)
    // 29-day cycle approximation, peaks at half-cycle (day 14.5)
    // Formula: ((day_of_year * 1000) / 29) % 1000
    // This gives a sawtooth 0-999, we want it to peak at 500
    uint32_t lunar_phase = ((uint32_t)day_of_year * 1000UL / 29) % 1000;
    int16_t lunar_deviation = abs((int16_t)lunar_phase - 500);  // Distance from peak
    uint8_t lunar_score = (uint8_t)(100 - (lunar_deviation / 5));  // Peak at 500 → 100, edges → 0
    
    // Variance component (40%)
    // Map activity variance (0-1000) to score (0-100)
    // Higher variance indicates more varied/active movement patterns
    uint16_t clamped_variance = (activity_variance > 1000) ? 1000 : activity_variance;
    uint8_t variance_score = (uint8_t)(clamped_variance / 10);
    
    // Blend: 40% circadian + 20% lunar + 40% variance
    uint16_t em = (uint16_t)(((uint32_t)circ_score * 40 + 
                              (uint32_t)lunar_score * 20 + 
                              (uint32_t)variance_score * 40) / 100);
    
    // Safety clamp (should never exceed 100 with proper inputs)
    if (em > 100) em = 100;
    
    return (uint8_t)em;
}

#endif // PHASE_ENGINE_ENABLED
