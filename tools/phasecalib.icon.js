// phasecalib.icon.js — Launcher icon for Phase Calib Logger
// Upload this to Bangle.js 2 Storage, then evaluate it
// (select it in Storage and click "Send to Espruino")
// It creates phasecalib.img (48×48 1-bit bitmap)

var g = Graphics.createArrayBuffer(48, 48, 1, {msb:true});
g.setColor(1); // white on black

// Pulse waveform — baseline y=30, QRS spike at x=22,y=8
g.drawLine(0,30, 10,30);
g.drawLine(10,30, 12,26); g.drawLine(12,26, 14,26);
g.drawLine(14,26, 16,30); g.drawLine(16,30, 18,30);
g.drawLine(18,30, 20,36); g.drawLine(20,36, 22,8);  // QRS
g.drawLine(22,8, 24,38); g.drawLine(24,38, 28,28);
g.drawLine(28,28, 31,30); g.drawLine(31,30, 34,22);
g.drawLine(34,22, 37,22); g.drawLine(37,22, 38,30);
g.drawLine(38,30, 48,30);

// Sensor-eye reticle at QRS peak (22,8)
g.drawCircle(22, 8, 4);
g.drawLine(18,8, 19,8); g.drawLine(25,8, 26,8);
g.drawLine(22,4, 22,5); g.drawLine(22,11, 22,12);

// Save to storage
require("Storage").write("phasecalib.img", g.asImage("string"));
console.log("Icon saved: phasecalib.img");
