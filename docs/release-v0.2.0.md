# v0.2.0

Magic Trackpad 3 USB-C supports USB and Bluetooth with 125 Hz resampling,
Windows gestures, and flick momentum. The installer chooses the native
x64 or ARM64 driver. Driver-only ZIPs are available for manual installation.

- Bluetooth report delivery preserves order across timer and packet callbacks.
  Delayed packets no longer rewind the sampling clock, and shutdown drains
  outstanding transport reads before resume.
- USB and Bluetooth release stale contacts after sustained packet loss.
- Legacy SPI and T2 USB handle malformed packets, request completion, report
  initialization, and scan time more safely. HID feature handlers check lengths.
- Setup stores uninstall ownership under protected Program Files and preserves
  packages that were already staged manually.

The x64 and ARM64 builds and regression suites passed. Magic Trackpad 3
hardware testing on x64 confirmed USB and Bluetooth input, flick momentum,
and Bluetooth reconnect. Live ARM64 testing remains outstanding. The Bluetooth
filter still uses the legacy HID dispatch patch; it is replaced in v0.2.1
(see [v0.2.1 notes](release-v0.2.1.md)).

This release uses WDK test signing. Prepare Windows to load test-signed
drivers and verify your download with `SHA256SUMS.txt`.
[Installation and recovery guide](https://github.com/Sager1145/mac-precision-touchpad/blob/master/docs/installer.md).
