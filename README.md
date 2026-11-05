# Mac Precision Touchpad for Magic Trackpad 3

Windows Precision Touchpad drivers for Apple trackpads, maintained by [Sager1145](https://github.com/Sager1145) and based on [imbushuo/mac-precision-touchpad](https://github.com/imbushuo/mac-precision-touchpad).

This fork focuses on **Magic Trackpad 3 USB-C over USB and Bluetooth**: smooth continuous movement and dragging, two-finger scrolling with flick momentum, Windows multi-finger gestures, and reliable Bluetooth recovery.

## Download and install

**[Download v0.2.0-preview.1](https://github.com/Sager1145/mac-precision-touchpad/releases/tag/v0.2.0-preview.1)** — Windows 10/11 x64.

Choose `mac-precision-touchpad-v0.2.0-preview.1-x64-setup.exe`. The installer provides a wizard, previous-driver backups, logs, upgrades and an Installed apps uninstall entry. A manual ZIP and SHA-256 checksums are also available.

**This is a test-signed preview, not a Microsoft/WHQL production-signed driver.** Windows must already allow test-signed drivers. The installer checks this and stops if preparation is needed. It does not change boot configuration, Secure Boot, BitLocker or Memory Integrity. Certificate trust is an explicit optional task.

Read the **[installation and recovery guide](docs/installer.md)** before installing. The [older manual test-signing guide](docs/test-signed-installation.md) describes the first USB-only preview.

1. Prepare Windows for test-signed drivers and reboot as described in the guide.
2. Verify the download checksum and run Setup as administrator.
3. Select the public certificate trust task if the test certificate is not already trusted.
4. Connect USB-C, or pair the trackpad through Windows Bluetooth settings. Unplug USB to use Bluetooth.
5. Configure taps, scrolling and multi-finger gestures in Windows **Settings > Bluetooth & devices > Touchpad** (Windows 11) or **Settings > Devices > Touchpad** (Windows 10).

The public signing certificate thumbprint is `94A13BEE28D788FC4A7FA91C568968C7DBADDC69`. No private key is included in downloads.

## What works

| Feature | Magic Trackpad 3 USB-C |
| --- | --- |
| USB pointing and click-dragging | Hardware tested |
| Bluetooth pointing and click-dragging | Hardware tested |
| Two-finger scrolling and flick momentum | Tested on both transports |
| Windows multi-finger gestures | Tested on Bluetooth; shared contact reporting on USB |
| Matching USB/Bluetooth feel | Confirmed by the user after direct comparison |
| Bluetooth off/on reconnect | Two successful hardware test cycles |
| Installer / uninstaller | Signed Windows wizard with package ownership checks |

Both transports use 125 Hz output, a 12 ms interpolation delay and measured-position history. Contact and button transitions are immediate. Lift-off is reported once at the last delivered position so Windows can preserve momentum. Prediction is bounded; there is no position low-pass filter that changes motion amplitude.

Bluetooth uses device timestamps to preserve buffered packet spacing and retries transient transport errors without restarting the gesture. USB uses a high-resolution native waitable timer. Radio latency and Windows scheduling can still differ.

See [USB/Bluetooth parity](docs/transport-parity.md), [Bluetooth implementation and testing](docs/bluetooth-trackpad-3.md), and the [release notes](docs/releases/v0.2.0-preview.1.md).

## Device support

The package retains upstream USB, SPI, Bluetooth and T2 MacBook device support. Magic Trackpad 2 shares the Type 5 protocol. This release's hardware testing was on **Magic Trackpad 3 USB-C (product 0324)**; other models are not newly certified or independently tested by this fork. Windows ARM64 binaries and Magic Trackpad 1 Bluetooth are not included in this release.

Legacy device lists and project history are preserved in the [upstream README snapshot](docs/upstream-readme.md). See the [upstream PR review](docs/upstream-pr-review.md) for selected fixes and changes deferred pending compatible hardware.

## Troubleshooting and uninstall

- Pair in Windows settings, then unplug USB when checking Bluetooth.
- If upgrading while disconnected, reconnect the trackpad to select the new package.
- Check Device Manager for a healthy Apple trackpad and Precision Touchpad child device.
- If Setup stops, read its message and logs in `C:\ProgramData\Sager1145\MacPrecisionTouchpad`.
- Uninstall through Windows Installed apps. Only packages recorded by this installer are removed; backups, logs and certificate trust remain for recovery.

Report the Windows build, trackpad model, connection type and reproduction steps in [Issues](https://github.com/Sager1145/mac-precision-touchpad/issues). Avoid posting Bluetooth addresses or other device identifiers unnecessarily.

## Build and test

Use Windows x64, Enterprise WDK 10.0.28000.0 and GCC for the standalone regression harnesses. The default EWDK location is `D:\BuildEnv`; pass `-EwdkRoot` if yours differs.

```powershell
.\tests\interpolation\run.ps1
.\tests\bluetooth\run.ps1
.\tests\legacy-usb\run.ps1
.\tests\transport-parity\run.ps1
.\tests\installer\run.ps1
.\tests\build-release-x64.ps1
.\installer\Build-Installer.ps1
```

The installer build needs Inno Setup 6.7.3, Windows SignTool and the local signing certificate/private key. It verifies driver signatures and catalog membership, signs helpers and Setup/uninstall, and generates a manual ZIP, manifest and checksums. See [installer build details](docs/installer.md#build-the-installer).

The build retains `SkipPackageVerification=true` for legacy INF compatibility; successful builds and test signatures do not imply current InfVerif or WHQL certification.

## License and acknowledgements

USB code is [GPLv2](LICENSE-GPL.md); SPI code is [MIT](LICENSE-MIT.md). See [license details](LICENSE.md). Original implementation by imbushuo and upstream contributors, with protocol work informed by [magictrackpad2-dkms](https://github.com/robbi5/magictrackpad2-dkms) and [macbook12-spi-driver](https://github.com/cb22/macbook12-spi-driver).
