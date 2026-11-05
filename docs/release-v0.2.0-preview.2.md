# v0.2.0-preview.2

This release adds signed ARM64 drivers for Magic Trackpad 3 over USB and
Bluetooth, with the same 125 Hz resampling and flick momentum behavior as x64.

- Setup treats an already-staged package as a successful install after checking
  the exact driver INF hash. Genuine PnP failures still surface as errors.
- One Setup EXE chooses the native driver for x64 or ARM64 Windows.
- Separate driver-only ZIPs provide the compiled binaries, INFs, catalogs,
  public certificate and manual installation instructions for each architecture.
- The installer regression suite now runs on Windows PowerShell 5.1 and covers
  the already-staged package response.

Both driver targets built with zero warnings and errors. PE architecture,
Authenticode signatures and catalog membership were verified. Live install,
uninstall and reinstall were exercised on x64. ARM64 hardware testing remains
outstanding.

Download the Setup EXE or the matching driver ZIP and verify its SHA-256 hash.
This preview uses WDK test signing and requires a test-signing-enabled Windows
machine. [Installation and recovery guide](https://github.com/Sager1145/mac-precision-touchpad/blob/master/docs/installer.md).
