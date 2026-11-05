# v0.2.2 (unreleased)

Fixes from a privacy and security review of v0.2.1. The x64 and ARM64 builds
and regression suites pass. A 0.2.2 test build was confirmed on x64 with Magic
Trackpad 3 over Bluetooth and USB, including the new Bluetooth start check.

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
