/*
 * Phase Engine Calibration Logger v2 — Bangle.js 2
 * ==================================================
 * HDLS-branded 5-zone UI with warm grayscale palette.
 * Records multi-sensor data to CSV for cross-referencing
 * against the Sensor Watch F91W Phase Engine firmware.
 *
 * Design: 0r4cl3 spec → r3nd3r visual production → spl1c3 assembly
 * Date:    2026-05-29
 *
 * Usage:
 *   1. Upload to Bangle.js 2 via Espruino Web IDE
 *   2. Launch from app launcher — amber icon with pulse waveform + sensor-eye
 *   3. Runs for 7 days, sampling every 30 seconds
 *   4. Data saved to phase_calib_0.csv on watch flash
 *   5. Download via Web IDE Storage tab
 *
 * Data columns (CSV):
 *   ts, accel_x_g, accel_y_g, accel_z_g, accel_mag_g, accel_diff_g,
 *   hrm_bpm, hrm_confidence, hrm_raw, hrm_filt, ppg_raw, ppg_offset,
 *   temp_c, pressure_hpa,
 *   gps_lat, gps_lon, gps_fix
 *
 * Hardware: Bangle.js 2 (nRF52840, KX022 accel, VC31B HRM,
 *           BMP280/SPL06 baro, AT6558 GPS)
 * Display:  176x176 3-bit reflective LCD (8 physical colors)
 * Storage:  8MB external flash (~288 KB/day → ~28 days max)
 */


// ═══════════════════════════════════════════════════════════
// 1. LAUNCHER ICON — r3nd3r production · 48×48
//    Amber (#FFB800) on Root Dark (#0E0B02)
//    Pulse waveform + sensor-eye reticle at QRS peak
// ═══════════════════════════════════════════════════════════

function drawIcon(g) {
  "use strict";

  g.setBgColor(0.05, 0.04, 0.01);  // #0E0B02 Root Dark
  g.clear();

  g.setColor(1.0, 0.72, 0.0);      // #FFB800 Amber Primary

  // Pulse waveform — baseline y=30, active x=10..38, QRS peak x=22,y=8
  // Flat baseline: left segment
  g.drawLine(0, 30, 10, 30);
  // P-wave: small upward bump
  g.drawLine(10, 30, 12, 26);
  g.drawLine(12, 26, 14, 26);
  g.drawLine(14, 26, 16, 30);
  // PR segment: short flat
  g.drawLine(16, 30, 18, 30);
  // Q-wave: small downward dip
  g.drawLine(18, 30, 20, 36);
  // R-wave: the sharp QRS spike
  g.drawLine(20, 36, 22, 8);
  // S-wave: plunge back
  g.drawLine(22, 8, 24, 38);
  // ST return toward baseline
  g.drawLine(24, 38, 28, 28);
  g.drawLine(28, 28, 31, 30);
  // T-wave: wider, gentler bump
  g.drawLine(31, 30, 34, 22);
  g.drawLine(34, 22, 37, 22);
  g.drawLine(37, 22, 38, 30);
  // Flat baseline: right segment
  g.drawLine(38, 30, 48, 30);

  // Sensor-eye reticle — center (22,8), radius 4px, crosshair 2px arms
  g.drawCircle(22, 8, 4);
  g.drawLine(18, 8, 20, 8);
  g.drawLine(24, 8, 26, 8);
  g.drawLine(22, 4, 22, 6);
  g.drawLine(22, 10, 22, 12);

  // Ambient calibration ticks at waveform boundaries
  g.drawLine(2, 30, 2, 32);
  g.drawLine(46, 30, 46, 28);
}


// ═══════════════════════════════════════════════════════════
// 2. COLOR PALETTE — Bangle.js 3-bit LCD mapping
//    Warm grayscale stops, HDLS-named, even luminance distribution
//    Use C.<NAME> with setC() throughout — never bare g.setColor
// ═══════════════════════════════════════════════════════════

var C = {
  ROOT:      0,   // #000000   0% — full background
  TRACE:     1,   // #333333  20% — panel backgrounds, zone separators
  DIM:       2,   // #595959  35% — status bar bg, quadrant bg
  TERTIARY:  3,   // #808080  50% — labels, reference lines, dividers
  SECONDARY: 4,   // #A6A6A6  65% — sub-readings, coords, sample counter
  MID:       5,   // #CCCCCC  80% — vector values, waveform trace
  BRIGHT:    6,   // #EBEBEB  92% — primary reading (HRM), recording dot
  HOT:       7    // #FFFFFF 100% — status bar text, active highlights
};

// RGB lookups (0-1 floats mapped to closest 3-bit LCD color)
var _C_RGB = [
  [0, 0, 0],           // 0: ROOT
  [0.2, 0.2, 0.2],     // 1: TRACE
  [0.35, 0.35, 0.35],  // 2: DIM
  [0.5, 0.5, 0.5],     // 3: TERTIARY
  [0.65, 0.65, 0.65],  // 4: SECONDARY
  [0.8, 0.8, 0.8],     // 5: MID
  [0.92, 0.92, 0.92],  // 6: BRIGHT
  [1, 1, 1]            // 7: HOT
];

function setC(c) {
  "use strict";
  var rgb = _C_RGB[c];
  g.setColor(rgb[0], rgb[1], rgb[2]);
}


// ═══════════════════════════════════════════════════════════
// 3. CONFIGURATION
// ═══════════════════════════════════════════════════════════

var SAMPLE_INTERVAL_SEC = 30;
var GPS_INTERVAL_SEC = 300;
var RECORDING_DURATION_SEC = 7 * 24 * 3600;
var MIN_FREE_BYTES = 500000;
var FILE_PREFIX = "phase_calib_";

// Waveform ring buffer: 40 samples, 4px each = 160px across 176px
var WAVEFORM_SAMPLES = 40;


// ═══════════════════════════════════════════════════════════
// 4. STATE
// ═══════════════════════════════════════════════════════════

var file = null;
var sampleCount = 0;
var recordingStart = 0;
var lastGpsTime = 0;
var lastSampleTime = 0;

var hrmBpm = 0;
var hrmConfidence = 0;
var hrmRaw = 0;
var hrmFilt = 0;
var ppgRaw = 0;
var ppgOffset = 0;

var latestAccel = { x: 0, y: 0, z: 0, diff: 0, mag: 0 };
var latestTemp = 0;
var latestPressure = 0;
var latestGps = { lat: 0, lon: 0, fix: 0 };

var gpsPowerOn = false;
var lowStorage = false;

var sampleTimer = null;
var gpsTimer = null;
var displayTimer = null;

// Waveform ring buffer — accelerometer magnitude history
var waveRing = new Array(WAVEFORM_SAMPLES);
var waveIdx = 0;
var waveCount = 0;  // number of samples pushed (up to WAVEFORM_SAMPLES)

// Recording dot blink state
var recDotVisible = true;

// Sub-frame counter for status bar (rolls at 10 fps)
var frameTick = 0;


// ═══════════════════════════════════════════════════════════
// 5. RENDERING — ZONES 0–4
//    Rendering order per spec:
//    1. Fill canvas with ROOT
//    2. Zone 0 status bar bg (TRACE)
//    3. Zone 4 waveform bg (TRACE)
//    4. Zone 2 quadrant backgrounds (DIM)
//    5. Waveform reference lines + trace
//    6. All text (zones 0,1,2,3)
//    7. Recording dot (on top of status bar)
// ═══════════════════════════════════════════════════════════

// ── Zone 0: Status Bar (y=0..12) ────────────────────

function drawZone0() {
  // Background bar
  setC(C.TRACE);
  g.fillRect(0, 0, 176, 12);

  // Elapsed time string
  var elapsed = getTime() - recordingStart;
  var d = Math.floor(elapsed / 86400);
  elapsed -= d * 86400;
  var h = Math.floor(elapsed / 3600);
  elapsed -= h * 3600;
  var m = Math.floor(elapsed / 60);
  var s = elapsed % 60;
  var ff = frameTick % 10;

  var ts = d + "d " +
    ("0" + h).substr(-2) + ":" +
    ("0" + m).substr(-2) + ":" +
    ("0" + s).substr(-2) + ":" +
    ("0" + ff).substr(-1);

  // "REC" label + elapsed time — left-aligned, 6x8, Hot
  setC(C.HOT);
  g.setFont("6x8", 1);
  g.setFontAlign(-1, -1);
  g.drawString("REC  " + ts, 14, 2);
}

// ── Zone 1: Primary Reading (y=16..48) ──────────────

function drawZone1() {
  var reading, unit;

  if (hrmBpm > 0 && hrmConfidence > 30) {
    reading = String(hrmBpm);
    unit = "bpm";
  } else {
    reading = latestAccel.mag.toFixed(2);
    unit = "g";
  }

  // Large centered Vector value (~28px)
  setC(C.BRIGHT);
  g.setFont("Vector", 30);
  g.setFontAlign(0, -1);
  g.drawString(reading, 88, 16);

  // Unit label below (~18px)
  setC(C.SECONDARY);
  g.setFont("Vector", 18);
  g.drawString(unit, 88, 46);

  // Heart bar: 7 segments, filled by confidence (0-100)
  drawHeartBar(hrmConfidence);
}

function drawHeartBar(confidence) {
  var segs = 7;
  var filled = Math.round(confidence / 100 * segs);
  // Each segment: 8px wide × 12px tall, 2px gap
  // Total width: 7*8 + 6*2 = 68px, centered
  var barX0 = 88 - 34;
  var barY = 64;

  for (var i = 0; i < segs; i++) {
    var sx = barX0 + i * 10;
    if (i < filled) {
      setC(C.MID);
      g.fillRect(sx, barY, sx + 7, barY + 11);
    } else {
      setC(C.DIM);
      g.drawRect(sx, barY, sx + 7, barY + 11);
    }
  }
}

// ── Zone 2: Sensor Grid (y=80..136) ─────────────────

function drawZone2() {
  // Quadrant layout: 2×2 grid, 4px gap between columns and rows
  // QX0=14 ensures left-aligned sub-readings stay inside bezel-safe x≥20
  var QW = 78;  // quadrant width (78×2 + 4 = 160, fits 176 with 8px margins)
  var QH = 26;  // quadrant height
  var QX0 = 14; // left edge (safe from circular bezel)
  var QGX = 4;  // column gap
  var QY0 = 80; // top edge of first row
  var QGY = 4;  // row gap

  // Quadrant backgrounds (DIM) — drawn once in drawBackgrounds()
  // Text rendered here

  // Q0: TEMP (top-left)
  drawQuadLabel("TEMP", QX0, QY0, QW);
  drawQuadValue(
    latestTemp.toFixed(1),
    QX0 + Math.floor(QW / 2),
    QY0 + 8
  );
  // °C suffix
  setC(C.SECONDARY);
  g.setFont("6x8", 1);
  g.setFontAlign(-1, -1);
  g.drawString("C", QX0 + Math.floor(QW / 2) + 18, QY0 + 12);
  // Thin rule
  setC(C.TERTIARY);
  g.drawLine(QX0 + 4, QY0 + 20, QX0 + QW - 4, QY0 + 20);
  // Delta sub-reading
  setC(C.SECONDARY);
  g.setFont("6x8", 1);
  g.setFontAlign(0, -1);
  g.drawString("+0.3", QX0 + Math.floor(QW / 2), QY0 + 22);

  // Q1: PRESS (top-right)
  var Q1X = QX0 + QW + QGX;
  drawQuadLabel("PRESS", Q1X, QY0, QW);
  drawQuadValue(
    latestPressure.toFixed(0),
    Q1X + Math.floor(QW / 2),
    QY0 + 8
  );
  setC(C.SECONDARY);
  g.setFont("6x8", 1);
  g.setFontAlign(-1, -1);
  g.drawString("hPa", Q1X + Math.floor(QW / 2) + 14, QY0 + 12);
  setC(C.TERTIARY);
  g.drawLine(Q1X + 4, QY0 + 20, Q1X + QW - 4, QY0 + 20);
  setC(C.SECONDARY);
  g.setFontAlign(0, -1);
  g.drawString("-2", Q1X + Math.floor(QW / 2), QY0 + 22);

  // Q2: ACCEL (bottom-left)
  var QY1 = QY0 + QH + QGY;
  drawQuadLabel("ACCEL", QX0, QY1, QW);
  drawQuadValue(
    latestAccel.mag.toFixed(2),
    QX0 + Math.floor(QW / 2),
    QY1 + 8
  );
  setC(C.SECONDARY);
  g.setFont("6x8", 1);
  g.setFontAlign(-1, -1);
  g.drawString("g", QX0 + Math.floor(QW / 2) + 18, QY1 + 12);
  setC(C.TERTIARY);
  g.drawLine(QX0 + 4, QY1 + 20, QX0 + QW - 4, QY1 + 20);
  // xyz sub-read on 3 lines
  setC(C.SECONDARY);
  g.setFontAlign(-1, -1);
  g.drawString("x:" + latestAccel.x.toFixed(2), QX0 + 6, QY1 + 22);
  g.drawString("y:" + latestAccel.y.toFixed(2), QX0 + 6, QY1 + 30);
  g.drawString("z:" + latestAccel.z.toFixed(2), QX0 + 6, QY1 + 38);

  // Q3: GPS (bottom-right)
  var Q3X = QX0 + QW + QGX;
  drawQuadLabel("GPS", Q3X, QY1, QW);
  if (latestGps.fix) {
    drawQuadValue(
      String(latestGps.fix),
      Q3X + Math.floor(QW / 2),
      QY1 + 8
    );
  } else {
    setC(C.SECONDARY);
    g.setFont("Vector", 16);
    g.setFontAlign(0, -1);
    g.drawString("---", Q3X + Math.floor(QW / 2), QY1 + 8);
  }
  setC(C.SECONDARY);
  g.setFont("6x8", 1);
  g.setFontAlign(-1, -1);
  g.drawString("sats", Q3X + Math.floor(QW / 2) + 8, QY1 + 12);
  setC(C.TERTIARY);
  g.drawLine(Q3X + 4, QY1 + 20, Q3X + QW - 4, QY1 + 20);
  // Lat/lon sub-line
  setC(C.SECONDARY);
  g.setFontAlign(-1, -1);
  if (latestGps.fix) {
    var ll = latestGps.lat.toFixed(4) + "," + latestGps.lon.toFixed(4);
    g.drawString(ll, Q3X + 6, QY1 + 22);
  } else {
    g.drawString("ACQUIRING", Q3X + 6, QY1 + 22);
  }
}

function drawQuadLabel(label, x, y, width) {
  setC(C.TERTIARY);
  g.setFont("6x8", 1);
  g.setFontAlign(0, -1);
  g.drawString(label, x + Math.floor(width / 2), y);
}

function drawQuadValue(value, cx, y) {
  setC(C.MID);
  g.setFont("Vector", 16);
  g.setFontAlign(0, -1);
  g.drawString(value, cx, y);
}

// ── Zone 3: Sample Counter (y=136..152) ─────────────

function drawZone3() {
  // Faint separator line above (Trace)
  setC(C.TRACE);
  g.drawLine(0, 136, 176, 136);

  // Comma-formatted sample count, right-aligned, 6x8
  setC(C.SECONDARY);
  g.setFont("6x8", 1);
  g.setFontAlign(1, -1);
  g.drawString("SAMPLES: " + fmtComma(sampleCount), 172, 140);
}

function fmtComma(n) {
  if (n === 0) return "0";
  var s = "";
  while (n > 0) {
    var chunk = String(n % 1000);
    if (n >= 1000 && chunk.length < 3) {
      chunk = ("00" + chunk).substr(-3);
    }
    s = chunk + (s ? "," : "") + s;
    n = Math.floor(n / 1000);
  }
  return s;
}

// ── Zone 4: Waveform Strip (y=152..176) ─────────────

function drawZone4() {
  // Reference lines: +2g, 1.0g, 0g (y=156, 164, 172)
  setC(C.TERTIARY);
  var refY = [156, 164, 172];
  for (var i = 0; i < 3; i++) {
    // Dashed line: draw alternating 4px segments
    for (var dx = 0; dx < 176; dx += 8) {
      g.drawLine(dx, refY[i], dx + 3, refY[i]);
    }
  }

  // Waveform sparkline — 40 samples, 4px per sample
  if (waveCount < 2) return;  // need at least 2 points

  setC(C.MID);

  // Map g-value to y: 0g→172, 1g→164, 2g→156
  function g2y(gv) {
    var y = 172 - (gv * 8);  // 0→172, 1→164, 2→156
    return Math.max(156, Math.min(172, Math.round(y)));
  }

  // Draw from oldest to newest (left to right)
  // Ring buffer: waveIdx points to NEXT write slot
  // Oldest data is at waveIdx (if not wrapped) or waveIdx (if wrapped)
  var start, count;
  if (waveCount < WAVEFORM_SAMPLES) {
    start = 0;
    count = waveCount;
  } else {
    start = waveIdx;        // oldest in ring
    count = WAVEFORM_SAMPLES;
  }

  var prevX = 8;  // start position with bezel-safe left margin
  var prevY = g2y(waveRing[start]);

  for (var i = 1; i < count; i++) {
    var ri = (start + i) % WAVEFORM_SAMPLES;
    var x = 8 + i * 4;  // 4px per sample
    var y = g2y(waveRing[ri]);
    if (x > 168) x = 168;  // bezel-safe right margin
    g.drawLine(prevX, prevY, x, y);
    prevX = x;
    prevY = y;
  }
}

// ── Backgrounds: zones 0, 2, 4 (drawn before text) ─

function drawBackgrounds() {
  // Zone 0 status bar bg (TRACE)
  setC(C.TRACE);
  g.fillRect(0, 0, 176, 12);

  // Zone 4 waveform bg (TRACE)
  setC(C.TRACE);
  g.fillRect(0, 152, 176, 176);

  // Zone 2 quadrant backgrounds (DIM)
  // QX0=14 ensures bezel-safe margins (x≥20 for text)
  var QW = 78, QH = 26;
  var QX0 = 14, QY0 = 80, QGX = 4, QGY = 4;
  var QY1 = QY0 + QH + QGY;

  setC(C.DIM);
  // Q0: TEMP
  g.fillRect(QX0, QY0, QX0 + QW, QY0 + QH);
  // Q1: PRESS
  g.fillRect(QX0 + QW + QGX, QY0, QX0 + 2 * QW + QGX, QY0 + QH);
  // Q2: ACCEL
  g.fillRect(QX0, QY1, QX0 + QW, QY1 + QH);
  // Q3: GPS
  g.fillRect(QX0 + QW + QGX, QY1, QX0 + 2 * QW + QGX, QY1 + QH);
}

// ── Full Display Render ─────────────────────────────

function drawDisplay() {
  // 1. Fill canvas with ROOT (clears everything including corners)
  setC(C.ROOT);
  g.clear();

  // 2. Zone backgrounds (before text, as spec)
  drawBackgrounds();

  // 3. Waveform reference lines + trace (Zone 4, drawn before text)
  drawZone4();

  // 4. All text zones (top to bottom)
  drawZone0();
  drawZone1();
  drawZone2();
  drawZone3();

  // 5. Recording dot (on top of status bar, blinking every second)
  if (recDotVisible) {
    setC(C.BRIGHT);
    g.fillCircle(6, 6, 2);
  }

  // Swap buffer to display
  g.flip();
}


// ═══════════════════════════════════════════════════════════
// 6. SENSOR EVENT HANDLERS (from v1 pipeline)
// ═══════════════════════════════════════════════════════════

Bangle.on('HRM', function(h) {
  hrmBpm = h.bpm;
  hrmConfidence = h.confidence;
});

Bangle.on('HRM-raw', function(h) {
  hrmRaw = h.raw || 0;
  hrmFilt = h.filt || 0;
  ppgRaw = (h.vcPPG !== undefined) ? h.vcPPG : 0;
  ppgOffset = (h.vcPPGoffs !== undefined) ? h.vcPPGoffs : 0;
});

Bangle.on('accel', function(a) {
  latestAccel.x = a.x;
  latestAccel.y = a.y;
  latestAccel.z = a.z;
  latestAccel.diff = a.diff;
  latestAccel.mag = a.mag;

  // Push to waveform ring buffer
  waveRing[waveIdx] = a.mag;
  waveIdx = (waveIdx + 1) % WAVEFORM_SAMPLES;
  if (waveCount < WAVEFORM_SAMPLES) waveCount++;
});

Bangle.on('GPS', function(g) {
  latestGps.lat = g.lat;
  latestGps.lon = g.lon;
  latestGps.fix = g.fix;
});

function samplePressure() {
  try {
    Bangle.getPressure().then(function(p) {
      latestTemp = p.temperature;
      latestPressure = p.pressure;
    }).catch(function(e) {
      console.log("Pressure read error: " + e);
    });
  } catch(e) {
    console.log("getPressure not available: " + e);
    latestTemp = E.getTemperature();
  }
}


// ═══════════════════════════════════════════════════════════
// 7. STORAGE + CSV
// ═══════════════════════════════════════════════════════════

function checkStorage() {
  var stats = require("Storage").getStats();
  if (stats && stats.freeBytes < MIN_FREE_BYTES) {
    lowStorage = true;
    return false;
  }
  return true;
}

function openFile() {
  var name = FILE_PREFIX + "0.csv";
  try {
    file = require("Storage").open(name, "a");
    console.log("Opened: " + name);
  } catch(e) {
    console.log("Fatal: cannot create file: " + e);
    file = null;
  }
}

function writeSample() {
  if (!file) return;
  if (lowStorage && !checkStorage()) return;

  var now = getTime();
  var row = [
    now,
    latestAccel.x.toFixed(4),
    latestAccel.y.toFixed(4),
    latestAccel.z.toFixed(4),
    latestAccel.mag.toFixed(4),
    latestAccel.diff.toFixed(4),
    hrmBpm,
    hrmConfidence,
    hrmRaw,
    hrmFilt,
    ppgRaw,
    ppgOffset,
    latestTemp.toFixed(1),
    latestPressure.toFixed(0),
    gpsPowerOn ? latestGps.lat.toFixed(6) : "",
    gpsPowerOn ? latestGps.lon.toFixed(6) : "",
    gpsPowerOn ? latestGps.fix : ""
  ];

  try {
    file.write(row.join(",") + "\n");
    sampleCount++;
  } catch(e) {
    console.log("Write error: " + e);
    lowStorage = true;
  }

  lastSampleTime = now;
}


// ═══════════════════════════════════════════════════════════
// 8. GPS POWER CYCLE
// ═══════════════════════════════════════════════════════════

function gpsCycle() {
  if (!gpsPowerOn) {
    Bangle.setGPSPower(1);
    gpsPowerOn = true;
    console.log("GPS on");
    setTimeout(function() {
      Bangle.setGPSPower(0);
      gpsPowerOn = false;
      console.log("GPS off");
    }, 60000);
  }
}


// ═══════════════════════════════════════════════════════════
// 9. RECORDING LIFECYCLE
// ═══════════════════════════════════════════════════════════

function startRecording() {
  recordingStart = getTime();

  openFile();
  if (!file) {
    E.showAlert("Failed to create file").then(function() {
      load();
    });
    return;
  }

  // CSV header
  var header = [
    "ts",
    "accel_x_g", "accel_y_g", "accel_z_g", "accel_mag_g", "accel_diff_g",
    "hrm_bpm", "hrm_confidence", "hrm_raw", "hrm_filt", "ppg_raw", "ppg_offset",
    "temp_c", "pressure_hpa",
    "gps_lat", "gps_lon", "gps_fix"
  ];
  file.write(header.join(",") + "\n");

  // Startup metadata
  var fwVer = "unknown";
  try { fwVer = process.env.VERSION; } catch(e) {}
  var startupInfo = [
    "# Phase Engine Calibration Logger v2.0",
    "# Started: " + new Date(recordingStart * 1000).toISOString(),
    "# Interval: " + SAMPLE_INTERVAL_SEC + "s",
    "# Duration: " + (RECORDING_DURATION_SEC / 86400) + " days",
    "# Firmware: " + fwVer,
    "# Flash free: " + require("Storage").getStats().freeBytes + " bytes",
    ""
  ];
  file.write(startupInfo.join("\n"));

  // Enable sensors
  Bangle.setHRMPower(1);
  Bangle.setBarometerPower(1);
  Bangle.setLCDTimeout(0);
  Bangle.setLocked(false);

  samplePressure();

  var bat = "unknown";
  try { bat = E.getBattery() + "%"; } catch(e) {}
  file.write("# Battery at start: " + bat + "\n");

  // Haptic confirmation
  Bangle.buzz(200, 0.5);
  setTimeout(function() { Bangle.buzz(200, 0.5); }, 300);

  // Display update timer: every 1 second for live status bar + frame counter
  displayTimer = setInterval(function() {
    frameTick++;
    recDotVisible = (frameTick % 2 === 0);
    drawDisplay();
  }, 1000);

  // Sample write timer: every SAMPLE_INTERVAL_SEC
  sampleTimer = setInterval(function() {
    if (!checkStorage()) {
      stopRecording("Storage full");
      return;
    }

    var elapsed = getTime() - recordingStart;
    if (elapsed >= RECORDING_DURATION_SEC) {
      stopRecording("Duration complete");
      return;
    }

    samplePressure();

    if (sampleCount > 0 && sampleCount % 120 === 0) {
      try {
        file.write("# Battery: " + E.getBattery() + "% at sample " + sampleCount + "\n");
      } catch(e) {}
    }

    writeSample();
  }, SAMPLE_INTERVAL_SEC * 1000);

  // GPS cycle timer
  gpsTimer = setInterval(gpsCycle, GPS_INTERVAL_SEC * 1000);

  // First sample after 2s warmup
  samplePressure();
  setTimeout(writeSample, 2000);

  // Initial display
  drawDisplay();

  console.log("Calibration logger v2 started. Interval=" + SAMPLE_INTERVAL_SEC +
    "s, Duration=" + (RECORDING_DURATION_SEC/86400) + "d");
}

function stopRecording(reason) {
  if (displayTimer) {
    clearInterval(displayTimer);
    displayTimer = null;
  }
  if (sampleTimer) {
    clearInterval(sampleTimer);
    sampleTimer = null;
  }
  if (gpsTimer) {
    clearInterval(gpsTimer);
    gpsTimer = null;
  }

  Bangle.setHRMPower(0);
  Bangle.setBarometerPower(0);
  Bangle.setGPSPower(0);
  gpsPowerOn = false;
  Bangle.setLCDTimeout(10);

  if (file) {
    var footer = [
      "",
      "# Recording stopped: " + new Date(getTime() * 1000).toISOString(),
      "# Reason: " + reason,
      "# Total samples: " + sampleCount,
      "# Duration: " + fmtHMS(getTime() - recordingStart)
    ];
    file.write(footer.join("\n"));
    file = null;
    console.log("File closed. " + sampleCount + " samples written.");
  }

  // Completion screen
  setC(C.ROOT);
  g.clear();

  setC(C.BRIGHT);
  g.setFont("Vector", 24);
  g.setFontAlign(0, 0);
  g.drawString("Recording", 88, 64);
  g.drawString("Complete", 88, 90);

  setC(C.SECONDARY);
  g.setFont("6x8", 1);
  g.drawString(reason, 88, 112);
  g.drawString(sampleCount + " samples", 88, 126);
  g.drawString("Download CSV via Web IDE", 88, 140);

  g.flip();

  Bangle.buzz(200, 0.5);
  setTimeout(function() { Bangle.buzz(200, 0.5); }, 300);
  setTimeout(function() { Bangle.buzz(400, 0.5); }, 600);
}

function fmtHMS(sec) {
  var h = Math.floor(sec / 3600);
  var m = Math.floor((sec % 3600) / 60);
  var s = sec % 60;
  return h + "h" + ("0" + m).substr(-2) + "m" + ("0" + s).substr(-2) + "s";
}


// ═══════════════════════════════════════════════════════════
// 10. BUTTON: LONG-PRESS TO STOP
// ═══════════════════════════════════════════════════════════

var btnPressTime = 0;
setWatch(function(e) {
  if (e.state) {
    btnPressTime = getTime();
  } else {
    var held = getTime() - btnPressTime;
    if (held >= 2) {
      stopRecording("Manual stop");
    }
  }
}, BTN1, { repeat: true, edge: "both" });


// ═══════════════════════════════════════════════════════════
// 11. STARTUP
// ═══════════════════════════════════════════════════════════

if (Bangle.removeAllListeners) {
  Bangle.removeAllListeners('swipe');
  Bangle.removeAllListeners('touch');
  Bangle.removeAllListeners('drag');
}

// Splash screen — branded amber-on-dark
setC(C.ROOT);
g.clear();

setC(C.BRIGHT);
g.setFont("Vector", 22);
g.setFontAlign(0, 0);
g.drawString("Phase", 88, 60);
g.drawString("Calibration", 88, 84);

setC(C.SECONDARY);
g.setFont("6x8", 1);
g.drawString("Logger v2.0", 88, 106);
g.drawString("Starting...", 88, 118);

g.flip();

setTimeout(startRecording, 1500);


// ═══════════════════════════════════════════════════════════
// END — bangle_calib_logger_v2.js
// Design: 0r4cl3 → r3nd3r → spl1c3 · 2026-05-29
// Brand: HDLS amber phosphor on 3-bit reflective LCD
// ═══════════════════════════════════════════════════════════
