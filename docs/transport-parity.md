# Magic Trackpad 3 USB and Bluetooth behavior

Both transports use the same interaction policy:

| Behavior | USB and Bluetooth |
| --- | --- |
| Continuous output | 125 Hz (8 ms timer period) |
| Measured-position interpolation delay | 12 ms |
| Fallback prediction | Filtered velocity, at most 15 ms beyond the newest sample |
| History | 8 measured samples per contact |
| Position filtering | None; measured motion amplitude is preserved |
| Click, contact down/up and gesture changes | Immediate, outside the periodic cadence |
| Lift-off | Once per contact, at its last delivered position |
| Contact capacity | Five active contacts, stable IDs, existing contacts prioritized |
| Physical touch and click detection | Shared Type 5 decoder; dedicated touch state and button bit 0 |
| Coordinate range and reported physical size | 7612 by 5065 units, 160 by 114.9 mm |

Windows continues to handle gesture interpretation and inertial scrolling.
The driver does not synthesize fingers or extra scrolling after lift-off.

## Parity corrections (2026-11-04)

USB now uses the same integer-safe Type 5 packet decoder as Bluetooth. This
aligns button masking, upper/lower coordinate bounds, malformed-packet rejection
and contact selection when near-field records or reordered fingers are present.
Previously USB treated any nonzero button byte as a click and selected the
first five physical records even if some were not touching.

USB also discards stale history and velocity after a gap of 200 ms or more,
matching Bluetooth. Bluetooth rounds the complete predicted coordinate rather
than truncating its displacement, matching USB's rounding policy in either
direction of motion.

Bluetooth retains its device-timestamp timeline and read recovery. USB retains
its native high-resolution waitable timer. Those transport-specific mechanisms
handle different packet delivery and scheduling conditions; neither promises
identical radio/cable latency or perfectly periodic output under all system loads.

## Verification

```powershell
.\tests\transport-parity\run.ps1
.\tests\interpolation\run.ps1
.\tests\bluetooth\run.ps1
.\tests\legacy-usb\run.ps1
.\tests\build-release-x64.ps1
```

All passed. The parity harness compiles the actual USB packet handler and
interpolation/delivery helpers alongside the production Bluetooth decoder,
resampler and release normalizer. It makes 160 comparisons covering movement
and reversals, click-dragging, two/three/five-finger input, reordered contacts,
near-field records, non-button header bits, lift-off, ID reuse and long gaps.
Variable packet intervals also exercise bounded prediction. Report states and
contact ordering match; coordinates differ by at most one device unit (less
than 0.03 mm), allowing for USB floating-point and kernel integer arithmetic.
This checks the reports handed to Windows, not Windows' internal momentum model.

Release x64 rebuilt with zero warnings and zero errors using the existing build
script. The combined package and binaries have valid local WDK test signatures.
Previous packages were exported before installing the combined package as
`oem229.inf`. Bluetooth's registered service binary was verified against the
rebuilt SYS by SHA-256, and all present Trackpad 3 devices reported healthy.
After connecting USB-C, its device also selected `oem229.inf`, reported healthy,
and the installed package's USB DLL matched the signed build by SHA-256.
The optional standalone package was also staged as `oem230.inf`; no second
installation was needed because the combined package already supplied it.

After installation the user compared pointing, dragging, scrolling and flick
momentum over both transports and confirmed: "They feel the same, and momentum
works."
