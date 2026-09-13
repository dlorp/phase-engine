# Second Movement Builder

Web-based firmware configuration tool for Second Movement watch firmware.

## Quick Start

1. **Open:** `file:///path/to/phase-engine/builder/index.html` in your browser
2. **Configure:** Select watch faces, adjust settings, and set homebase location
3. **Build:** Click "Generate Firmware" to download custom firmware
4. **Flash:** Follow flashing instructions for your hardware

---

## Homebase Location

The **Homebase Location** section configures location-specific seasonal data for circadian-aware watch faces.

### What is Homebase?

Homebase provides your watch with:
- **Expected daylight hours** for each day of year
- **Average temperature** patterns for your location
- **Seasonal energy baselines** for circadian rhythm tracking

This data is compiled into your firmware as a lookup table (~9KB).

### Which Faces Use It?

Currently in development:
- **Circadian Score Face** (future integration)
- **Sleep Tracker Face** (future integration)
- **Phase Display Face** (planned)

If you're not using these faces, you can leave homebase blank.

### How to Configure

#### Option 1: Use Browser Location (Recommended)

1. Click **"[USE BROWSER LOCATION]"** button
2. Allow location access when prompted
3. Latitude, longitude, and timezone will auto-fill

**Note:** This requires HTTPS or localhost. File:// URLs may not support geolocation.

#### Option 2: Manual Entry

Enter your approximate location:

1. **Latitude** (-90 to 90)
   - Examples:
     - Seattle: `47.6062`
     - London: `51.5074`
     - Tokyo: `35.6762`
     - Sydney: `-33.8688` (negative for southern hemisphere)

2. **Longitude** (-180 to 180)
   - Examples:
     - Seattle: `-122.3321` (negative for western hemisphere)
     - London: `-0.1278`
     - Tokyo: `139.6503`
     - Sydney: `151.2093`

3. **Timezone**
   - **Format 1 (Abbreviations):** Use standard timezone codes
     - Examples: `AKST`, `PST`, `MST`, `CST`, `EST`, `HST`
     - Also supports daylight variants: `AKDT`, `PDT`, `MDT`, `CDT`, `EDT`
   - **Format 2 (UTC Offset):** Use `UTC+X` or `UTC-X`
     - Examples: `UTC-9`, `UTC-8`, `UTC-5`, `UTC+0`, `UTC+9`
   - **Format 3 (Raw Minutes):** Offset in minutes (advanced)
     - Examples: `-540` (UTC-9), `-480` (UTC-8), `+540` (UTC+9)

### How It Works

When you download firmware:

1. Builder passes homebase data to the build system
2. `utils/generate_homebase.py` generates a location-specific table
3. Table is compiled into `lib/phase/homebase_table.h`
4. Watch faces can query seasonal data at runtime

### Accuracy Requirements

- **Latitude/Longitude:** Nearest city is fine (±50 miles accuracy is OK)
- **Timezone:** Must match your actual timezone for correct calculations

### Privacy

Your location data:
- ✅ Stays local (never sent to a server)
- ✅ Compiled into firmware (not stored separately)
- ✅ Only used for seasonal calculations
- ❌ Does NOT track your movements

---

## Settings

### 24H MODE
Display time in 24-hour format (e.g., 14:00) instead of 12-hour (e.g., 2:00 PM).

### BUTTON SOUND
Enable/disable audible beep when pressing buttons.

### LED COLORS
Customize RGB LED colors for signals and notifications.

### ACTIVE HOURS
Configure quiet hours when certain alerts are suppressed.

---

## Watch Face Selection

### How to Add Faces

1. Browse **"AVAILABLE"** faces (right panel)
2. Tap a face to add it to your configuration
3. Faces appear in **"ACTIVE"** list (left panel)

### How to Reorder Faces

1. Drag faces in the **"ACTIVE"** list
2. Order determines navigation sequence on watch
3. First face is your default/home screen

### How to Remove Faces

1. Click the **X** next to a face in **"ACTIVE"** list
2. Face returns to **"AVAILABLE"** pool

### Face Categories

- **Clock:** Time display variations
- **Sensor:** Data from onboard sensors (temperature, light, accelerometer)
- **Complication:** Additional info (sunrise/sunset, moon phase, etc.)
- **Alarm:** Timers, alarms, countdowns
- **Settings:** Watch configuration

---

## Templates

### Saving Configurations

1. Configure faces and settings as desired
2. Click **"Save Template"**
3. Enter a name (e.g., "Daily Driver", "Travel Setup")
4. Template saved to browser localStorage

### Loading Templates

1. Click **"Load Template"**
2. Select from saved templates or presets
3. Your current configuration is replaced

### Preset Templates

The builder includes pre-configured setups:
- **Minimal:** Basic time + essentials
- **Sensor Heavy:** All sensor-based faces
- **Productivity:** Timers, alarms, deadlines
- **Adventure:** Outdoor/travel features

---

## URL Sharing

Your configuration is encoded in the URL hash. You can:

- **Share:** Copy URL to share your setup with others
- **Bookmark:** Save URL for quick access to favorite configs
- **Version Control:** Track config changes in git

Example:
```
builder/index.html#faces=clock,sunrise,temp&24h=true&homebaseLat=47.6062&homebaseLon=-122.3321&homebaseTz=UTC-8
```

---

## Advanced

### Board Selection

Choose your hardware variant:
- **Sensor Watch:** Original design
- **Sensor Watch Pro:** Enhanced with accelerometer
- **Deep Sea:** Specialized variant

### Display Type

- **Classic:** Standard LCD segments
- **Custom:** Modified segment layouts (experimental)

### Build Locally

If you have the toolchain installed:

```bash
# From repository root
make BOARD=sensorwatch_pro DISPLAY=classic

# With homebase table
make BOARD=sensorwatch_pro DISPLAY=classic PHASE_ENGINE_ENABLED=1
```

---

## Troubleshooting

### "Use Browser Location" doesn't work

**Cause:** Geolocation requires HTTPS or localhost.

**Fix:** Run a local web server:
```bash
cd phase-engine/builder
python3 -m http.server 8000
# Open http://localhost:8000
```

### Invalid latitude/longitude values

**Symptoms:** Red border on input field

**Fix:**
- Latitude: Must be between -90 and 90
- Longitude: Must be between -180 and 180
- Use decimal degrees (not degrees/minutes/seconds)

### Timezone errors during firmware build

**Cause:** Using unsupported timezone abbreviation

**Fix:** Use one of these formats:
- **Abbreviations:** AKST, PST, MST, CST, EST, HST (and daylight variants)
- **UTC Offset:** UTC-9, UTC-8, UTC-5, UTC+9
- **Raw Minutes:** -540, -480, -300, +540

**Unsupported abbreviations:** International zones (CET, JST, etc.) - use UTC offset instead

### Configuration lost on page reload

**Cause:** Not saved as template

**Fix:**
1. Save your config as a template
2. Or bookmark the URL (config is in hash)

---

## Contributing

### Reporting Issues

Found a bug? [Open an issue](https://github.com/dlorp/phase-engine/issues) with:
- Browser version
- Steps to reproduce
- Expected vs actual behavior

### Adding Features

Builder architecture:
- `index.html` - Single-file app (HTML + CSS + JS)
- `face_registry.json` - Watch face metadata
- `settings_registry.json` - Global settings

---

## License

MIT License - See repository root for details.

---

**Quick Links:**
- [Phase Engine Documentation](../lib/phase/README.md)
- [Main Repository README](../README.md)
- [Build Instructions](../docs/)
