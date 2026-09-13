/*
 * MIT License
 *
 * Copyright (c) 2026 Diego Perez
 *
 * Phase Engine: Solarpunk Score Constants
 * 
 * Four vital signs of a cybernetic organism, named after
 * living-system rhythms rather than clinical metrics.
 * 
 * Each renders as a 2-character label on the segment LCD:
 *   Position 0: any character (almost)
 *   Position 1: limited to A,B,C,D,E,F,H,I,J,L,N,O,R,T,U,X
 *   All four score labels pass both constraints.
 */

#ifndef SCORES_H_
#define SCORES_H_

#ifdef PHASE_ENGINE_ENABLED

/*
 * Score layout on the segment LCD (positions 0-9):
 *
 *   [SO]  [Signal ▂▄▆██]   Sol   78   circadian alignment
 *   [DE]  [Signal ▂▂▄▆█]   Dew   62   stillness quality
 *   [SA]  [Signal ███▆▄▂]   Sap   88   movement density
 *   [HU]  [Signal ▂▂▂▂▂]   Hum   91   environmental harmony
 *
 * Signal indicator (vertical bars) shows graphical 0-100 level.
 * Clock digits (positions 4-9) show numeric value.
 */

// Score label strings — use with watch_display_text() or snprintf
#define SCORE_LABEL_SOL  "SO"   // Sol: circadian phase alignment with solar rhythm
#define SCORE_LABEL_DEW  "DE"   // Dew: stillness quality during sleep window
#define SCORE_LABEL_SAP  "SA"   // Sap: movement density vs personal 7-day baseline
#define SCORE_LABEL_HUM  "HU"   // Hum: environmental harmony (light + temp vs seasonal expectation)

// Score mapping — which internal metric feeds which solarpunk score
// SO ← circadian phase score (from phase_compute)
// DE ← 100 - restlessness index (from sleep_data_calc_restlessness)
// SA ← WK normalized against wk_baseline (from metrics→wk + baseline)
// HU ← comfort score (from metric_comfort_compute)

#endif // PHASE_ENGINE_ENABLED

#endif // SCORES_H_
