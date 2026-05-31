// phasecalib-setup.js — Run ONCE in REPL after uploading phasecalib.app.js
// Registers launcher entry + icon + default settings
// Copy/paste entire block into left side of Web IDE, press Enter

(function() {
  // 1. Icon — 48×48 1-bit, amber pulse waveform + sensor-eye reticle
  var g = Graphics.createArrayBuffer(48, 48, 1, {msb:true});
  g.setColor(1);
  // Pulse waveform with QRS spike
  g.drawLine(0,30, 10,30);
  g.drawLine(10,30, 12,26); g.drawLine(12,26, 14,26);
  g.drawLine(14,26, 16,30); g.drawLine(16,30, 18,30);
  g.drawLine(18,30, 20,36); g.drawLine(20,36, 22,8);
  g.drawLine(22,8, 24,38); g.drawLine(24,38, 28,28);
  g.drawLine(28,28, 31,30); g.drawLine(31,30, 34,22);
  g.drawLine(34,22, 37,22); g.drawLine(37,22, 38,30);
  g.drawLine(38,30, 48,30);
  g.drawCircle(22,8,4);
  g.drawLine(18,8,19,8); g.drawLine(25,8,26,8);
  g.drawLine(22,4,22,5); g.drawLine(22,11,22,12);

  require("Storage").write("phasecalib.img", g.asImage("string"));
  console.log("✓ Icon saved");

  // 2. Launcher registration
  require("Storage").write("phasecalib.info", {
    "id": "phasecalib",
    "name": "Phase Calib Logger",
    "src": "phasecalib.app.js",
    "icon": "phasecalib.img"
  });
  console.log("✓ Launcher entry created");

  // 3. Default settings
  require("Storage").writeJSON("phasecalib.json", {
    sampleInterval: 30,
    gpsInterval: 300,
    recordingDuration: 604800,
    minFreeBytes: 500000,
    brightness: 1,
    dimTimeout: 15,
    powerSaveBLE: false,
    sensors: ["accel","hrm","baro","gps"],
    filePrefix: "phase_calib_"
  });
  console.log("✓ Default settings saved");

  console.log("\nDone! Long-press to clock → press to open launcher → 'PhaseCal'");
})();
