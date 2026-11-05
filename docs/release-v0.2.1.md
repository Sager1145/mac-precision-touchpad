# v0.2.1

The Bluetooth filter no longer patches the shared Windows Bluetooth HID
driver, and this release fixes bugs found in a second full code review. The
installer still chooses the native x64 or ARM64 driver, and driver-only ZIPs
remain available for manual installation.

The x64 and ARM64 builds and regression suites passed. A pre-release test
build was confirmed on x64 with Magic Trackpad 3 over USB and Bluetooth. That
test ran before a reboot, so the v0.2.0 patch may still have been in memory.
A Bluetooth check after a reboot, and live ARM64 testing, remain outstanding.
**Reboot once after upgrading from v0.2.0 or earlier.**

## Bluetooth filter

- The filter no longer rewrites the Bluetooth HID transport's shared dispatch
  table. Its own requests reach the transport through a private, kernel-only
  device, so other Bluetooth keyboards and mice are unaffected. A reboot
  clears the patch left by v0.2.0 or earlier.
- Input resumes after a PnP stop/start (resource rebalance). Previously the
  device identity was cleared and the trackpad stayed silent.
- The read error counter resets on resume and reconfiguration, so one
  transient error no longer forces a full reconfigure.
- The resampling timer no longer depends on a large timestamp product that
  could overflow on long uptimes.

## All drivers

- The Precision Touchpad certification blob is now copied in full. Only its
  first byte was written before.
- Get-feature handlers check the report buffer and report its size.

## USB

- Mode-switch control transfers no longer read or write past their buffer.
  The transfer length is unchanged.
- HID string requests copy the string itself, instead of overwriting a stack
  pointer, and free the queried string.
- Set-feature requests return their real status and check the user-mode
  configuration report length.
- Resume retries the mode switch, instead of leaving the trackpad silent
  after one failed control transfer. Power-down stops the USB pipe before
  clearing interpolation state.

## T2 and SPI (not hardware tested)

- The T2 mode switch uses nonpaged memory and handles allocation failure.
  Continuous reader setup failures are reported instead of ignored.
- SPI stops its power-on retry timer before entering D3.

## Installer

- The install folder is fixed to Program Files, because Setup and the
  uninstaller run the installed scripts elevated.
- Certificate trust is checked before files are copied.
- Uninstall no longer fails when the ownership record is missing, or when
  Windows reused an owned `oemNN.inf` name for another package.
- Packages installed by the v0.2.0 previews are adopted if they still match
  their record and are signed by this project.
- Setup error messages keep their original language, and an old error is not
  shown for a new run.
- `pnputil` warnings on stderr no longer stop Windows PowerShell 5.1.
- The standalone `AmtPtpHidFilter.inf` is Bluetooth only and no longer
  rebinds Magic Trackpad 2 USB.

## Known open items

- T2 Macs: four product IDs appear in both the kernel and user-mode USB
  sections of `AmtPtpDevice.inf`. The correct binding needs T2 hardware.
- The Bluetooth filter keeps one transport read outstanding. Whether this
  drops reports under load needs an ETW trace on hardware.
- Selective reporting (button or surface off) is ignored by the T2, SPI and
  Bluetooth drivers.
- Contacts held across sleep are not released on resume.

This release uses WDK test signing. Prepare Windows to load test-signed
drivers and verify your download with `SHA256SUMS.txt`.
[Installation and recovery guide](https://github.com/Sager1145/mac-precision-touchpad/blob/master/docs/installer.md).
