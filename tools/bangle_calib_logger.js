/*
 * Phase Engine Calibration Logger for Bangle.js 2
 * ================================================
 * Records multi-sensor data to CSV for cross-referencing
 * against the Sensor Watch F91W Phase Engine firmware.
 *
 * Usage:
 *   1. Upload to Bangle.js 2 via Espruino Web IDE (Storage → New File)
 *   2. Launch from app launcher — starts recording immediately
 *   3. Runs for 7 days (configurable), 30-second sample interval
 *   4. Data saved to phase_calib_0.csv on watch flash
 *   5. Download via Web IDE Storage tab after recording completes
 *
 * Data columns (CSV):
 *   ts, accel_x_g, accel_y_g, accel_z_g, accel_mag_g, accel_diff_g,
 *   hrm_bpm, hrm_confidence, hrm_raw, hrm_filt, ppg_raw, ppg_offset,
 *   temp_c, pressure_hpa,
 *   gps_lat, gps_lon, gps_fix
 *
 * Hardware: Bangle.js 2 (nRF52840, KX022 accel, VC31B HRM, BMP280/SPL06 baro, AT6558 GPS)
 * Storage: 8MB external flash (~288 KB/day at 30s intervals → ~28 days max)
 * Power: ~4 days continuous with HRM+accel — keep charged during recording
 *
 * Based on patterns from:
 *   - Espruino GPS POI Logger (StorageFile append CSV)
 *   - hrmaccevents app (simultaneous HRM + accelerometer)
 *   - Bangle.js Data Streaming tutorial
 *   - Espruino Data Collection guide
 */

// ── Configuration ──────────────────────────────────────────
var SAMPLE_INTERVAL_SEC = 30;   // seconds between data rows
var GPS_INTERVAL_SEC = 300;     // GPS fix every 5 minutes (power-hungry)
var RECORDING_DURATION_SEC = 7 * 24 * 3600;  // 7 days
var MIN_FREE_BYTES = 500000;    // stop when < 500KB free
var FILE_PREFIX = "phase_calib_";  // output filename prefix

// ── State ──────────────────────────────────────────────────
var file = null;           // StorageFile handle
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

// ── Display ────────────────────────────────────────────────
function drawStatus() {
  g.clear();
  g.setFont("6x8", 2);
  g.setFontAlign(0, -1);

  // Title
  g.setColor("#FFD700"); // amber
  g.drawString("Phase Calib Logger", 88, 10);

  g.setFont("6x8", 1);
  g.setFontAlign(-1, -1);

  // Stats
  g.setColor("#FFFFFF");
  var elapsed = getTime() - recordingStart;
  var elapsedStr = fmtHMS(elapsed);
  g.drawString("Elapsed: " + elapsedStr, 10, 40);
  g.drawString("Samples: " + sampleCount, 10, 56);

  // Sensor status
  g.drawString("HRM:   " + hrmBpm + " bpm (" + hrmConfidence + "%)", 10, 78);
  g.drawString("Temp:  " + latestTemp.toFixed(1) + " C", 10, 94);
  g.drawString("Press: " + latestPressure.toFixed(0) + " hPa", 10, 110);

  // GPS
  var gpsStr = gpsPowerOn ? (latestGps.fix ? "FIX " + latestGps.lat.toFixed(4) : "no fix") : "off";
  g.drawString("GPS:   " + gpsStr, 10, 126);

  // Accel
  g.drawString("Accel: " + latestAccel.mag.toFixed(3) + " g", 10, 142);

  // Storage warning
  if (lowStorage) {
    g.setColor("#FF0000");
    g.drawString("LOW STORAGE — stopping soon", 10, 165);
    g.setColor("#FFFFFF");
  }

  // Recording indicator
  g.setColor("#00FF00");
  g.drawString("● RECORDING", 60, 10);

  g.flip();
}

// ── Helpers ────────────────────────────────────────────────
function fmtHMS(sec) {
  var h = Math.floor(sec / 3600);
  var m = Math.floor((sec % 3600) / 60);
  var s = sec % 60;
  return h + "h" + m.toString().padStart(2,'0') + "m" + s.toString().padStart(2,'0') + "s";
}

function checkStorage() {
  var stats = require("Storage").getStats();
  if (stats && stats.freeBytes < MIN_FREE_BYTES) {
    lowStorage = true;
    return false;
  }
  // Also warn at 2x threshold
  if (stats && stats.freeBytes < MIN_FREE_BYTES * 2) {
    // still warning only — don't stop yet
  }
  return true;
}

function openFile() {
  // StorageFile append mode auto-creates if file doesn't exist
  // (proven pattern from gpspoilog example)
  var name = FILE_PREFIX + "0.csv";
  try {
    file = require("Storage").open(name, "a");
    console.log("Opened: " + name + " (append mode)");
  } catch(e) {
    console.log("Fatal: cannot create file: " + e);
    file = null;
  }
}

// ── Sensor Event Handlers ──────────────────────────────────

// HRM: updates on every heartbeat (VC31B PPG sensor)
Bangle.on('HRM', function(h) {
  hrmBpm = h.bpm;
  hrmConfidence = h.confidence;
});

// HRM-raw: raw PPG waveform — fires at 25 Hz
// Fields vary by firmware version: raw/filt always present;
// vcPPG/vcPPGoffs available on 2v19+ (VC31B FIFO readout)
Bangle.on('HRM-raw', function(h) {
  hrmRaw = h.raw || 0;
  hrmFilt = h.filt || 0;
  ppgRaw = (h.vcPPG !== undefined) ? h.vcPPG : 0;
  ppgOffset = (h.vcPPGoffs !== undefined) ? h.vcPPGoffs : 0;
});

// Accelerometer: fires at default 12.5 Hz (can be changed with Bangle.setPollInterval)
Bangle.on('accel', function(a) {
  latestAccel.x = a.x;
  latestAccel.y = a.y;
  latestAccel.z = a.z;
  latestAccel.diff = a.diff;
  latestAccel.mag = a.mag;
});

// Pressure/Temperature: sampled on-demand (not continuous to save power)
function samplePressure() {
  try {
    Bangle.getPressure().then(function(p) {
      latestTemp = p.temperature;
      latestPressure = p.pressure;
    }).catch(function(e) {
      console.log("Pressure read error: " + e);
    });
  } catch(e) {
    // Bangle.getPressure() may not exist on older firmware
    console.log("getPressure not available: " + e);
    latestTemp = E.getTemperature(); // fallback: ±10°C accuracy
  }
}

// GPS: fires when powered on and tracking
Bangle.on('GPS', function(g) {
  latestGps.lat = g.lat;
  latestGps.lon = g.lon;
  latestGps.fix = g.fix;
});

// ── Write CSV Row ──────────────────────────────────────────
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
  drawStatus();
}

// ── Periodic GPS Power Toggle ──────────────────────────────
function gpsCycle() {
  if (!gpsPowerOn) {
    // Turn on GPS, get a fix, turn off after 60 seconds
    Bangle.setGPSPower(1);
    gpsPowerOn = true;
    console.log("GPS on");
    // Turn off after 60 seconds (enough for a warm-hot fix)
    setTimeout(function() {
      Bangle.setGPSPower(0);
      gpsPowerOn = false;
      console.log("GPS off");
    }, 60000);
  }
}

// ── Main Recording Loop ────────────────────────────────────
function startRecording() {
  recordingStart = getTime();

  // Open output file
  openFile();
  if (!file) {
    E.showAlert("Failed to create file").then(function() {
      load(); // exit app
    });
    return;
  }

  // Write CSV header
  var header = [
    "ts",
    "accel_x_g", "accel_y_g", "accel_z_g", "accel_mag_g", "accel_diff_g",
    "hrm_bpm", "hrm_confidence", "hrm_raw", "hrm_filt", "ppg_raw", "ppg_offset",
    "temp_c", "pressure_hpa",
    "gps_lat", "gps_lon", "gps_fix"
  ];
  file.write(header.join(",") + "\n");

  // Log startup info as comments (prefixed with # so CSV parsers ignore)
  var fwVer = "unknown";
  try { fwVer = process.env.VERSION; } catch(e) {}
  var startupInfo = [
    "# Phase Engine Calibration Logger v1.0",
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
  Bangle.setBarometerPower(1);  // BMP280/SPL06 — may be auto-powered by getPressure() but explicit is safer
  
  // Keep screen on during recording (so status is visible)
  Bangle.setLCDTimeout(0);
  Bangle.setLocked(false);
  
  // Take initial pressure reading
  samplePressure();

  // Log battery level
  var bat = "unknown";
  try { bat = E.getBattery() + "%"; } catch(e) {}
  file.write("# Battery at start: " + bat + "\n");

  // Flash LED to confirm start
  Bangle.buzz(200, 0.5);
  setTimeout(function() { Bangle.buzz(200, 0.5); }, 300);

  // Start sample timer (every SAMPLE_INTERVAL_SEC)
  sampleTimer = setInterval(function() {
    // Check storage
    if (!checkStorage()) {
      stopRecording("Storage full");
      return;
    }

    // Check duration
    var elapsed = getTime() - recordingStart;
    if (elapsed >= RECORDING_DURATION_SEC) {
      stopRecording("Duration complete");
      return;
    }

    // Sample pressure (async — value will be ready by next interval)
    samplePressure();

    // Log battery level every hour
    if (sampleCount > 0 && sampleCount % 120 === 0) {  // 120 samples × 30s = hourly
      try { file.write("# Battery: " + E.getBattery() + "% at sample " + sampleCount + "\n"); } catch(e) {}
    }

    // Write current sensor snapshot
    writeSample();
  }, SAMPLE_INTERVAL_SEC * 1000);

  // Start GPS cycle timer
  gpsTimer = setInterval(gpsCycle, GPS_INTERVAL_SEC * 1000);

  // First sample immediately
  samplePressure();
  setTimeout(writeSample, 2000); // wait 2s for sensors to warm up

  // Initial display
  drawStatus();

  console.log("Calibration logger started. Interval=" + SAMPLE_INTERVAL_SEC + "s, Duration=" + (RECORDING_DURATION_SEC/86400) + "d");
}

// ── Stop Recording ─────────────────────────────────────────
function stopRecording(reason) {
  if (sampleTimer) {
    clearInterval(sampleTimer);
    sampleTimer = null;
  }
  if (gpsTimer) {
    clearInterval(gpsTimer);
    gpsTimer = null;
  }

  // Turn off sensors
  Bangle.setHRMPower(0);
  Bangle.setBarometerPower(0);
  Bangle.setGPSPower(0);
  gpsPowerOn = false;

  // Restore normal screen timeout
  Bangle.setLCDTimeout(10);

  // Write footer
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

  // Display completion
  g.clear();
  g.setFont("6x8", 2);
  g.setFontAlign(0, 0);
  g.setColor("#FFD700");
  g.drawString("Recording Complete", 88, 70);
  g.setFont("6x8", 1);
  g.setColor("#FFFFFF");
  g.drawString(reason, 88, 90);
  g.drawString(sampleCount + " samples", 88, 110);
  g.drawString("Download CSV via Web IDE", 88, 130);
  g.flip();

  // Three buzzes to signal completion
  Bangle.buzz(200, 0.5);
  setTimeout(function() { Bangle.buzz(200, 0.5); }, 300);
  setTimeout(function() { Bangle.buzz(400, 0.5); }, 600);
}

// ── Button Handler (long-press to stop early) ──────────────
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

// ── Start ──────────────────────────────────────────────────
// Clean up any existing widgets from launcher
if (Bangle.removeAllListeners) {
  Bangle.removeAllListeners('swipe');
  Bangle.removeAllListeners('touch');
  Bangle.removeAllListeners('drag');
}

g.clear();
g.setFont("6x8", 2);
g.setFontAlign(0, 0);
g.setColor("#FFD700");
g.drawString("Phase Calibration", 88, 60);
g.setFont("6x8", 1);
g.setColor("#FFFFFF");
g.drawString("Logger v1.0", 88, 80);
g.drawString("Starting...", 88, 100);
g.flip();

// Start after a brief delay so the display renders
setTimeout(startRecording, 1500);
