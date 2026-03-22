# WLED 2D Matrix Configuration — 11×10 Word Clock

These files configure WLED to treat the 11×10 LED matrix (LEDs 4-113) as a
2D display so you can use WLED's built-in 2D effects as the background behind
the clock text.

## How to apply

### Step 1 — Configure the 2D matrix in WLED hardware settings

POST `matrix-hardware.json` to your device:

```
http://wled.local/json/cfg
```

You can do this from a browser, Postman, or curl.  In Windows PowerShell:

```powershell
Invoke-RestMethod -Uri "http://wled.local/json/cfg" `
  -Method POST `
  -ContentType "application/json" `
  -InFile "matrix-hardware.json"
```

This tells WLED the physical matrix is 11 columns × 10 rows.
You only need to do this once — it is saved to flash.

### Step 2 — Import the preset collection

In the WLED web interface:
  Presets → (hamburger menu) → Import from file → select `wordclock-presets.json`

This adds four ready-made presets:

| Preset | Background effect |
|--------|-------------------|
| WordClock – Dark     | Solid dark blue (classic word clock look) |
| WordClock – Rainbow  | Rolling rainbow 2D effect                 |
| WordClock – Plasma   | 2D plasma / lava-lamp effect              |
| WordClock – Sparkles | Twinkling sparkle effect across the matrix|

Select any preset then adjust the word clock colours under
Config → Usermods → wc.

### bgBrightness tip

The `bgBri` setting in the wc usermod controls how bright the background
effect shows through:
  0   → background completely black (classic look)
  64  → dim glow of the effect (default)
  128 → half-brightness effect
  255 → effect at full brightness, words still overlaid solid
