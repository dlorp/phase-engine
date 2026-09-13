/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Shared Circadian Cosine Lookup Table
 *
 * Single source of truth for the 24-hour circadian rhythm curve.
 * Previously duplicated across phase_engine.c, metric_em.c, and
 * metric_energy.c — with different phase anchors (8 PM, 10 AM)
 * that diverged from human circadian norm (2 PM peak alertness).
 *
 * Curve: cos(2π * (hour - 14) / 24) * 1000
 *   Peak:  hour 14 (2 PM)  = +1000 — maximum alertness
 *   Trough: hour 2  (2 AM)  = -1000 — minimum alertness
 *   Zero crossing: hour 8 (8 AM), hour 20 (8 PM)
 *
 * Usage:
 *   #include "circadian_lut.h"
 *   uint8_t chrono_hour = (hour + 24 - PHASE_CHRONOTYPE_OFFSET) % 24;
 *   int16_t curve = circadian_lut_24[chrono_hour];
 *
 * All three consumers (phase_engine, metric_em, metric_energy) now
 * use the curve directly — no negation. The chronotype offset shifts
 * the peak to the user's actual rhythm.
 */

#ifndef CIRCADIAN_LUT_H_
#define CIRCADIAN_LUT_H_

#include <stdint.h>

#ifdef PHASE_ENGINE_ENABLED

static const int16_t circadian_lut_24[24] = {
    -866,  // 00:00
    -966,  // 01:00
    -1000, // 02:00 (TROUGH — minimum alertness)
    -966,  // 03:00
    -866,  // 04:00
    -707,  // 05:00
    -500,  // 06:00
    -259,  // 07:00
    0,     // 08:00 (crossing upward — waking)
    259,   // 09:00
    500,   // 10:00
    707,   // 11:00
    866,   // 12:00
    966,   // 13:00
    1000,  // 14:00 (PEAK — maximum alertness)
    966,   // 15:00
    866,   // 16:00
    707,   // 17:00
    500,   // 18:00
    259,   // 19:00
    0,     // 20:00 (crossing downward — winding down)
    -259,  // 21:00
    -500,  // 22:00
    -707   // 23:00
};

#endif // PHASE_ENGINE_ENABLED

#endif // CIRCADIAN_LUT_H_
