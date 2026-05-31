/* JSON
---
id: phasecalib
name: Phase Calib Logger
shortName: PhaseCal
version: "3.10"
description: Multi-sensor calibration logger for Phase Engine — Bangle.js 2
icon: phasecalib.img
type: app
tags: tool,health,outdoors
supports: [BANGLEJS2]
storage:
  - {name: phasecalib.app.js, url: app.js}
  - {name: phasecalib.img, url: app-icon.js, evaluate: true}
  - {name: phasecalib.json, url: default.json}
---
*/
// ═══════════════════════════════════════════════════════════
// Phase Engine Calibration Logger v3.10
// Cherry-picked: recorder (modular), sleeplog (drawing),
//   nightwatch (periodic sampling), gipy (power), zambretti (Layout)
//
// POWER PROFILE (Bangle.js 2, 175 mAh battery):
//   HRM on:     +1.0 mA continuous
//   Accel on:   included in baseline (always running)
//   GPS 60s/5m: +0.5 mA average (GPS peaks at ~20mA for 60s)
//   Baro + CPU: +0.5 mA
//   Display on: +2.0 mA (at brightness=1)
//   ───────────────────────────────────
//   Total:      ~4.0 mA → ~44 hours battery at full brightness
//   Dim+timeout: ~2.0 mA → ~88 hours
//
// FOREGROUND-ONLY: Records only while app is open.
//   Long-press → return to clock = recording stops.
//   For background recording, pair with recorder-style widget.
// ═══════════════════════════════════════════════════════════

var S = require("Storage");

// ── Settings (recorder/nightwatch readJSON pattern) ────────
var SETTINGS_FILE = "phasecalib.json";
var DEFAULTS = {
  sampleInterval: 30,         // seconds between CSV rows
  gpsInterval: 300,           // GPS fix every N seconds
  recordingDuration: 7*24*3600, // 7 days max
  minFreeBytes: 500000,       // stop when < 500KB free
  brightness: 1,              // LCD brightness (0-1)
  lcdTimeout: 15,             // seconds before screen blanks
  wakeOnSample: true,         // relight screen on each sample write
  powerSaveBLE: false,        // disable BLE during recording
  sensors: ["accel","hrm","baro","gps"],
  filePrefix: "phase_calib_"
};

var settings;
try {
  settings = S.readJSON(SETTINGS_FILE, 1) || {};
  for (var k in DEFAULTS) {
    if (!(k in settings)) settings[k] = DEFAULTS[k];
  }
} catch(e) {
  settings = Object.assign({}, DEFAULTS);
}

// ── State ──────────────────────────────────────────────────
var file = null, sampleCount = 0, recordingStart = 0;
var hrmBpm = 0, hrmConfidence = 0, hrmRaw = 0, hrmFilt = 0;
var ppgRaw = 0, ppgOffset = 0;
var latestAccel = { x:0, y:0, z:0, diff:0, mag:0 };
var latestMag = { x:0, y:0, z:0, heading:0 };
var latestTemp = 0, latestPressure = 0;
var latestGps = { lat:0, lon:0, fix:0 };
var gpsPowerOn = false, lowStorage = false;
var sampleTimer = null, gpsTimer = null;
var screenTimer = null;
var recDotVisible = true, screenBlanked = false;
var waveRing = new Array(40), waveIdx = 0, waveCount = 0;
var metricIndex = 0;  // 0=HRM, 1=Accel, 2=Temp, 3=Press

// Min/max tracking (nightwatch pattern)
var minmax = {
  temp: { min: 99, max: -99 },
  pressure: { min: 99999, max: 0 },
  hrm: { min: 255, max: 0 },
  accel: { min: 99, max: 0 }
};

// ── HDLS Colors (3-bit RGB LCD) ────────────────────────────
var AMBER  = "#FFD700";  // yellow (110)
var CYAN   = "#00FFFF";  // cyan (011)
var WHITE  = "#FFFFFF";  // white (111)
var BLACK  = "#000000";  // black (000)

// ── Widget Integration ─────────────────────────────────────
Bangle.loadWidgets();
Bangle.drawWidgets();
var R = Bangle.appRect;

// ── Drawing State (sleeplog interruptible pattern) ─────────
var drawingID = 0;
var drawTimeout = null;

function scheduleDraw(ms) {
  if (drawTimeout) clearTimeout(drawTimeout);
  if (screenBlanked) return;  // don't draw into blank screen
  var thisID = ++drawingID;
  drawTimeout = setTimeout(function() {
    if (thisID === drawingID) drawDisplay();
  }, ms || 0);
}

// ── Screen Power Management (gipy pattern) ─────────────────
function wakeScreen() {
  screenBlanked = false;
  Bangle.setLCDBrightness(settings.brightness);
  Bangle.setLCDTimeout(settings.lcdTimeout);
  if (screenTimer) { clearTimeout(screenTimer); screenTimer = null; }
  scheduleDraw(50);
}

function onScreenBlank() {
  screenBlanked = true;
  Bangle.setLCDBrightness(0);
}

// ── Touch: tap center to cycle metric ───────────────────────
Bangle.on('touch', function(btn, xy) {
  if (!xy || screenBlanked) return;
  // Only respond to taps in center band (avoid top status bar + bottom waveform)
  if (xy.y > R.y+14 && xy.y < R.y2-28) {
    metricIndex = (metricIndex + 1) % 4;
    wakeScreen();
  }
});

// ── Display (event-driven, NOT periodic) ───────────────────
function drawStatusBar(elapsed) {
  elapsed = Math.floor(elapsed);
  g.setColor(AMBER);
  g.fillRect(R.x, R.y, R.x2, R.y+14);

  g.setColor(BLACK);
  g.setFont("6x8", 1);
  var d = Math.floor(elapsed / 86400);
  var h = Math.floor((elapsed % 86400) / 3600);
  var m = Math.floor((elapsed % 3600) / 60);
  var s = elapsed % 60;
  g.drawString(
    "REC  " + d + "d " +
    ("0"+h).substr(-2) + ":" +
    ("0"+m).substr(-2) + ":" +
    ("0"+s).substr(-2),
    R.x+12, R.y+2
  );

  // Sample counter (compact, right side of status bar)
  g.setFontAlign(1, -1);
  g.drawString("S" + sampleCount, R.x2-4, R.y+2);
  g.setFontAlign(-1, -1);

  if (recDotVisible) {
    g.setColor(AMBER);
    g.fillCircle(R.x+4, R.y+7, 3);
  }
}

function drawPrimaryReading() {
  var cx = R.x + (R.w>>1);
  var reading, unit, label;

  switch (metricIndex) {
    case 0: // HRM
      if (hrmBpm > 0 && hrmConfidence > 30) {
        reading = String(hrmBpm); unit = "bpm";
      } else {
        reading = "--"; unit = "bpm";
      }
      label = "HR";
      break;
    case 1: // Accel
      reading = latestAccel.mag.toFixed(2); unit = "g";
      label = "G";
      break;
    case 2: // Temp
      reading = latestTemp.toFixed(1); unit = "C";
      label = "T";
      break;
    case 3: // Press
      reading = latestPressure.toFixed(0); unit = "hPa";
      label = "P";
      break;
  }

  g.setColor(WHITE);
  g.setFont("Vector", 32); g.setFontAlign(0, -1);
  g.drawString(reading, cx, R.y+20);
  g.setColor(CYAN);
  g.setFont("Vector", 16);
  g.drawString(unit, cx, R.y+52);

  // Metric label (top-left of center area)
  g.setColor(AMBER);
  g.setFont("6x8", 1); g.setFontAlign(-1, -1);
  g.drawString(label, R.x+2, R.y+20);

  // Pips showing which metric is active
  var pipY = R.y+66, pipX0 = cx - 18;
  for (var p = 0; p < 4; p++) {
    if (p === metricIndex) {
      g.setColor(AMBER);
      g.fillCircle(pipX0 + p*12, pipY, 3);
    } else {
      g.setColor(CYAN);
      g.drawCircle(pipX0 + p*12, pipY, 3);
    }
  }

  // HRM confidence pips (when on HRM metric)
  if (metricIndex === 0 && hrmBpm > 0) {
    var segs = 7, filled = Math.round(hrmConfidence/100*segs);
    var barX = cx - 24, barY = R.y+72;
    g.setFont("4x6", 1); g.setFontAlign(0, -1);
    g.drawString("sig", cx, barY-8);
    for (var i = 0; i < segs; i++) {
      var sx = barX + i*8;
      if (i < filled) {
        g.setColor(AMBER); g.fillCircle(sx+2, barY+3, 2);
      } else {
        g.setColor(CYAN); g.drawCircle(sx+2, barY+3, 2);
      }
    }
  }
}

function drawSensorGrid() {
  var qw = (R.w-4)>>1, qh = 24;
  var qx0 = R.x, qy0 = R.y+80;

  function quad(x, y, w, h, label, val) {
    g.setColor(CYAN); g.fillRect(x, y, x+w-1, y+h-1);
    g.setColor(BLACK);
    g.setFont("6x8", 1); g.setFontAlign(0, -1);
    g.drawString(label, x+(w>>1), y+1);
    g.setColor(WHITE);
    g.setFont("Vector", 14);
    g.drawString(val, x+(w>>1), y+10);
  }

  quad(qx0, qy0, qw, qh, "TEMP", latestTemp.toFixed(1) + "C");
  quad(qx0+qw+4, qy0, qw, qh, "PRESS", latestPressure.toFixed(0) + "h");
  quad(qx0, qy0+qh+2, qw, qh, "ACCEL", latestAccel.mag.toFixed(2) + "g");

  g.setColor(CYAN);
  g.fillRect(qx0+qw+4, qy0+qh+2, R.x2, qy0+2*qh-3);
  g.setColor(BLACK);
  g.setFont("6x8", 1); g.setFontAlign(0, -1);
  g.drawString("GPS", qx0+qw+4+(qw>>1), qy0+qh+3);
  g.setColor(WHITE);
  g.setFont("Vector", 14);
  var gpsText = latestGps.fix ? latestGps.fix+" sats" : "---";
  if (latestMag.heading) {
    gpsText += " " + String.fromCharCode(
      [8593,8598,8594,8600,8595,8601,8592,8600][Math.round(latestMag.heading/45)%8] || 8593
    );
  }
  g.drawString(gpsText, qx0+qw+4+(qw>>1), qy0+qh+12);
}

function drawWaveform() {
  var wy0 = R.y2 - 24;
  g.setColor(AMBER);
  g.fillRect(R.x, wy0, R.x2, R.y2);

  if (waveCount < 2) return;

  // Build values array from ring buffer (oldest → newest)
  var vals = [];
  var start, cnt;
  if (waveCount < 40) { start = 0; cnt = waveCount; }
  else { start = waveIdx; cnt = 40; }
  for (var i = 0; i < cnt; i++) {
    vals.push(waveRing[(start + i) % 40]);
  }

  // Try graph module (nightwatch pattern), fall back to hand-drawn
  var useGraph = false;
  try { require("graph"); useGraph = true; } catch(e) {}

  if (useGraph) {
    require("graph").drawLine(g, vals, {
      x: R.x + 4, y: wy0 + 2,
      width: R.w - 8, height: 20,
      miny: minmax.accel.min,
      maxy: minmax.accel.max,
      axes: false,
      grid: false,
      color: BLACK
    });
  } else {
    // Hand-drawn fallback (works without graph module)
    g.setColor(BLACK);
    for (i = 0; i < 3; i++) {
      var ry = wy0 + 2 + i * 8;
      for (var dx = R.x; dx < R.x2; dx += 8)
        g.drawLine(dx, ry, dx + 3, ry);
    }
    var px = R.x + 4;
    var py = wy0 + 2 + 18 - (vals[0] - minmax.accel.min) /
      (minmax.accel.max - minmax.accel.min || 1) * 16;
    for (i = 1; i < vals.length; i++) {
      var x = R.x + 4 + i * ((R.w - 8) / (vals.length - 1));
      var y = wy0 + 2 + 18 - (vals[i] - minmax.accel.min) /
        (minmax.accel.max - minmax.accel.min || 1) * 16;
      y = Math.max(wy0 + 2, Math.min(wy0 + 18, Math.round(y)));
      g.drawLine(px, py, x, y);
      px = x; py = y;
    }
  }
}

function drawDisplay() {
  if (screenBlanked) return;
  var elapsed = recordingStart ? getTime()-recordingStart : 0;

  g.clear(1);

  drawStatusBar(elapsed);
  drawPrimaryReading();
  drawSensorGrid();
  drawWaveform();

  g.flip();
}

// ── Sensor Handlers ────────────────────────────────────────
Bangle.on('HRM', function(h) {
  hrmBpm = h.bpm; hrmConfidence = h.confidence;
  if (h.bpm > 0) {
    if (h.bpm < minmax.hrm.min) minmax.hrm.min = h.bpm;
    if (h.bpm > minmax.hrm.max) minmax.hrm.max = h.bpm;
  }
});
Bangle.on('HRM-raw', function(h) {
  hrmRaw = h.raw || 0; hrmFilt = h.filt || 0;
  ppgRaw = (h.vcPPG !== undefined) ? h.vcPPG : 0;
  ppgOffset = (h.vcPPGoffs !== undefined) ? h.vcPPGoffs : 0;
});
Bangle.on('accel', function(a) {
  latestAccel.x = a.x; latestAccel.y = a.y; latestAccel.z = a.z;
  latestAccel.diff = a.diff; latestAccel.mag = a.mag;
  waveRing[waveIdx] = a.mag;
  waveIdx = (waveIdx+1) % 40;
  if (waveCount < 40) waveCount++;
  if (a.mag < minmax.accel.min) minmax.accel.min = a.mag;
  if (a.mag > minmax.accel.max) minmax.accel.max = a.mag;
});
Bangle.on('GPS', function(gps) {
  latestGps.lat = gps.lat; latestGps.lon = gps.lon; latestGps.fix = gps.fix;
});
Bangle.on('mag', function(m) {
  latestMag.x = m.x; latestMag.y = m.y; latestMag.z = m.z;
  latestMag.heading = m.heading || 0;
});

function samplePressure() {
  try {
    Bangle.getPressure().then(function(p) {
      latestTemp = p.temperature;
      latestPressure = p.pressure;
      if (p.temperature < minmax.temp.min) minmax.temp.min = p.temperature;
      if (p.temperature > minmax.temp.max) minmax.temp.max = p.temperature;
      if (p.pressure < minmax.pressure.min) minmax.pressure.min = p.pressure;
      if (p.pressure > minmax.pressure.max) minmax.pressure.max = p.pressure;
    }).catch(function(){});
  } catch(e) {
    latestTemp = E.getTemperature();
  }
}

// ── Power: Screen blank handling ───────────────────────────
// Use Bangle.setLocked + Bangle.on('lock') to detect screen blank
Bangle.setLocked(false);
Bangle.setLCDTimeout(settings.lcdTimeout);

Bangle.on('lock', function(locked) {
  if (locked) {
    onScreenBlank();
  } else {
    wakeScreen();
  }
});

// ── Storage / CSV (recorder/gpspoilog pattern) ─────────────
function checkStorage() {
  if (S.getStats().freeBytes < settings.minFreeBytes) {
    lowStorage = true; return false;
  }
  return true;
}

function openFile() {
  // Ensure phasecalib/ directory exists
  try { S.write("phasecalib/.keep", ""); } catch(e) {}
  var now = new Date();
  var dateStr = now.getFullYear() +
    ("0"+(now.getMonth()+1)).substr(-2) +
    ("0"+now.getDate()).substr(-2);
  try { file = S.open("phasecalib/" + settings.filePrefix + dateStr + ".csv", "a"); }
  catch(e) { file = null; }
}

function writeSample() {
  if (!file || (lowStorage && !checkStorage())) return;
  var now = getTime();
  var row = [
    now,
    latestAccel.x.toFixed(4), latestAccel.y.toFixed(4),
    latestAccel.z.toFixed(4), latestAccel.mag.toFixed(4),
    latestAccel.diff.toFixed(4),
    hrmBpm, hrmConfidence, hrmRaw, hrmFilt, ppgRaw, ppgOffset,
    latestMag.x.toFixed(1), latestMag.y.toFixed(1),
    latestMag.z.toFixed(1), latestMag.heading,
    latestTemp.toFixed(1), latestPressure.toFixed(0),
    gpsPowerOn ? latestGps.lat.toFixed(6) : "",
    gpsPowerOn ? latestGps.lon.toFixed(6) : "",
    gpsPowerOn ? latestGps.fix : ""
  ];
  try {
    file.write(row.join(",") + "\n");
    sampleCount++;
  } catch(e) { lowStorage = true; }

  // Event-driven display: update screen on each sample
  recDotVisible = !recDotVisible;
  if (settings.wakeOnSample) wakeScreen();
  else scheduleDraw(0);
}

// ── GPS Cycle ──────────────────────────────────────────────
function gpsCycle() {
  if (!gpsPowerOn) {
    Bangle.setGPSPower(1); gpsPowerOn = true;
    setTimeout(function() {
      Bangle.setGPSPower(0); gpsPowerOn = false;
    }, 60000);
  }
}

// ── Recording Lifecycle ────────────────────────────────────
function startRecording() {
  recordingStart = getTime();
  openFile();
  if (!file) {
    E.showAlert("Failed to create file").then(function() { load(); });
    return;
  }

  // CSV header
  var header = [
    "ts","accel_x_g","accel_y_g","accel_z_g","accel_mag_g","accel_diff_g",
    "hrm_bpm","hrm_confidence","hrm_raw","hrm_filt","ppg_raw","ppg_offset",
    "mag_x","mag_y","mag_z","mag_heading",
    "temp_c","pressure_hpa","gps_lat","gps_lon","gps_fix"
  ];
  file.write(header.join(",") + "\n");

  // Power management (gipy pattern)
  if (settings.powerSaveBLE) {
    try { NRF.setAdvertising({}); } catch(e) {}
  }

  Bangle.setHRMPower(1);
  Bangle.setBarometerPower(1);
  Bangle.setCompassPower(1);
  wakeScreen();

  // Haptic
  Bangle.buzz(200, 0.5);
  setTimeout(function() { Bangle.buzz(200, 0.5); }, 300);

  // NOTE: No 1Hz display timer. Display updates ONLY on:
  //   - Sample write (every settings.sampleInterval seconds)
  //   - Touch/lock unlock (wakeScreen)
  // This saves ~2mA vs continuous refresh.

  // Sample timer (nightwatch periodic pattern)
  sampleTimer = setInterval(function() {
    if (!checkStorage()) { stopRecording("Storage full"); return; }
    if (getTime()-recordingStart >= settings.recordingDuration) {
      stopRecording("Duration complete"); return;
    }
    samplePressure();
    if (sampleCount > 0 && sampleCount % 120 === 0) {
      try {
        file.write("# Battery: " + E.getBattery() +
          "% at sample " + sampleCount + "\n");
      } catch(e) {}
    }
    writeSample();
  }, settings.sampleInterval * 1000);

  // GPS timer
  gpsTimer = setInterval(gpsCycle, settings.gpsInterval * 1000);

  // First sample after warmup
  samplePressure();
  setTimeout(writeSample, 2000);
  scheduleDraw(0);
}

function stopRecording(reason) {
  if (sampleTimer) { clearInterval(sampleTimer); sampleTimer = null; }
  if (gpsTimer) { clearInterval(gpsTimer); gpsTimer = null; }
  if (screenTimer) { clearTimeout(screenTimer); screenTimer = null; }

  Bangle.setHRMPower(0);
  Bangle.setBarometerPower(0);
  Bangle.setCompassPower(0);
  Bangle.setGPSPower(0);
  gpsPowerOn = false;
  Bangle.setLCDBrightness(0);
  Bangle.setLCDTimeout(10);  // restore normal timeout
  Bangle.setLocked(true);    // allow normal lock behavior

  if (file) {
    file.write(
      "\n# Recording stopped: " + new Date(getTime()*1000).toISOString() +
      "\n# Reason: " + reason +
      "\n# Total samples: " + sampleCount + "\n"
    );
    file = null;
  }

  // Completion screen with min/max (nightwatch pattern)
  g.clear(1);
  g.setColor(AMBER);
  g.setFont("Vector", 20); g.setFontAlign(0, 0);
  g.drawString("Recording", R.x+(R.w>>1), R.y+20);
  g.drawString("Complete", R.x+(R.w>>1), R.y+44);
  g.setColor(WHITE);
  g.setFont("6x8", 1); g.setFontAlign(0, -1);
  g.drawString(reason, R.x+(R.w>>1), R.y+68);
  g.drawString(sampleCount + " samples", R.x+(R.w>>1), R.y+82);
  // Min/max summary
  g.setColor(CYAN);
  var t = minmax.temp, p = minmax.pressure;
  var h = minmax.hrm, a = minmax.accel;
  g.drawString(
    "T:" + t.min.toFixed(0) + "-" + t.max.toFixed(0) + "C  " +
    "P:" + p.min.toFixed(0) + "-" + p.max.toFixed(0),
    R.x+(R.w>>1), R.y+102
  );
  g.drawString(
    "HR:" + (h.min<255?h.min:"--") + "-" + (h.max||"--") + "bpm  " +
    "G:" + a.min.toFixed(1) + "-" + a.max.toFixed(1),
    R.x+(R.w>>1), R.y+118
  );
  g.setColor(CYAN);
  g.setFont("6x8", 1);
  g.drawString("Download CSV via Web IDE", R.x+(R.w>>1), R.y+140);
  g.flip();

  Bangle.buzz(200, 0.5);
  setTimeout(function() { Bangle.buzz(200, 0.5); }, 300);
  setTimeout(function() { Bangle.buzz(400, 0.5); }, 600);
}

// ── Button: long-press to stop (setUI preferred over setWatch) ─
var btnPressTime = 0;
try {
  setWatch(function(e) {
    if (e.state) {
      btnPressTime = getTime();
    } else {
      if (getTime() - btnPressTime >= 2) {
        Bangle.buzz(25);
        stopRecording("Manual stop");
      }
    }
  }, BTN1, { repeat:true, edge:"both" });
} catch(e) {
  console.log("setWatch unavailable, using setUI");
  Bangle.setUI({ mode:"custom", btn:function() {
    Bangle.buzz(25);
    stopRecording("Manual stop");
  }});
}

// ── Splash → Start ─────────────────────────────────────────
g.clear(1);
g.setColor(AMBER);
g.setFont("Vector", 22); g.setFontAlign(0, 0);
g.drawString("Phase", R.x+(R.w>>1), R.y+(R.h>>1)-16);
g.drawString("Calibration", R.x+(R.w>>1), R.y+(R.h>>1)+8);
g.setColor(WHITE);
g.setFont("6x8", 1); g.setFontAlign(0, -1);
g.drawString("Logger v3.10", R.x+(R.w>>1), R.y+(R.h>>1)+32);
g.setColor(CYAN);
g.drawString("Starting...", R.x+(R.w>>1), R.y+(R.h>>1)+46);
g.flip();

setTimeout(startRecording, 1500);
