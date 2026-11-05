# v0.2.2

Magic Trackpad now appears in Windows Settings with a touchpad icon and its
Bluetooth battery level. This release also fixes the findings of a privacy and
security review of v0.2.1.

The x64 and ARM64 builds and regression suites passed. Test builds were
confirmed on x64 with Magic Trackpad 3 over Bluetooth and USB, including the
new Bluetooth start check, the touchpad icon and the battery level over
Bluetooth. The touchpad icon over USB and live ARM64 testing remain
outstanding.

## Settings: icon and battery

- The driver package declares the trackpad's device container a touchpad
  (`Input.Digitizer.Touchpad`), so Settings shows a trackpad instead of a
  mouse. Windows applies this on Windows 11 24H2 (26100.3323) and later.
  MacBook built-in trackpads are excluded, because they share the computer's
  container.
- Windows does not read battery levels from classic Bluetooth HID devices.
  Setup now installs the `AmtPtpBattery` service. Every five minutes and when
  the trackpad connects, it requests the trackpad's battery report through
  the standard Windows HID API and stores the percentage where Settings ›
  Bluetooth & devices reads it. It runs as LocalSystem with only the basic
  service privilege, from Program Files. Uninstall removes it.
- The Bluetooth filter also passes on battery reports the trackpad sends by
  itself while charging. The filter never requests the report itself: a test
  build that did crashed Windows inside HidBth.

## Installer

- A malformed preview-era ownership record in `C:\ProgramData`, which any
  user can create, no longer makes Setup fail after the driver is installed.
- If `pnputil` stages the package and then fails, the package is still
  recorded, so uninstall can remove it.
- Drivers are removed only after the uninstall confirmation. If removal
  fails, the uninstall stops and the app stays installed.
- The certificate task now states that Windows will trust anything signed
  with the key. Uninstall removes the trust if Setup added it; trust that was
  already present is left alone. The guide explains how to remove it by hand.
- The state folder, including its logs, is limited to SYSTEM and
  Administrators, and logs are capped.
- The elevated scripts load PowerShell modules only from the system location.

## Drivers

- The Bluetooth filter checks that the HIDCLASS structure it reads belongs to
  the transport driver and that the handler lies inside that driver's image.
  If not, it fails to start instead of calling an unknown address.
- Device-name requests are tracked, so power-down can cancel them, and are
  refused once the transport route is gone.
- The unused device interfaces are removed from all four drivers. Nothing used
  them; applications reach the trackpad through the standard HID interface.
- Raw touch packets and delivered reports are traced only at verbose level
  under their own keyword. SPI per-contact positions moved to verbose.
- Binaries record only the PDB file name, not the build machine's path.

## Unchanged

- Signatures still use the WDK test certificate and carry no timestamp, and
  `SHA256SUMS.txt` is not separately signed.

This release uses WDK test signing. Prepare Windows to load test-signed
drivers and verify your download with `SHA256SUMS.txt`.
[Installation and recovery guide](https://github.com/Sager1145/mac-precision-touchpad/blob/master/docs/installer.md).
