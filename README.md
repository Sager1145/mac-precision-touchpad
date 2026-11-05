# Mac Precision Touchpad for Magic Trackpad 3

Windows Precision Touchpad drivers for Apple trackpads, maintained by [Sager1145](https://github.com/Sager1145) and based on [imbushuo/mac-precision-touchpad](https://github.com/imbushuo/mac-precision-touchpad).

This fork focuses on **Magic Trackpad 3 USB-C over USB and Bluetooth**: smooth continuous movement and dragging, two-finger scrolling with flick momentum, Windows multi-finger gestures, and reliable Bluetooth recovery.

## Download and install

**[Download v0.2.2](https://github.com/Sager1145/mac-precision-touchpad/releases/tag/v0.2.2)** for Windows 10/11 x64 and ARM64.

Choose `mac-precision-touchpad-v0.2.2-setup.exe`. Setup selects the native x64 or ARM64 package automatically. It provides previous-driver backups, logs, upgrades and an Installed apps uninstall entry. SHA-256 checksums are available for every download.

**This release uses WDK test signing.** Prepare Windows to load test-signed drivers before running Setup. The installer checks the active policy and offers certificate trust as an optional task.

Read the **[installation and recovery guide](docs/installer.md)** before installing. The [older manual test-signing guide](docs/test-signed-installation.md) describes the first USB-only preview.

1. Prepare Windows for test-signed drivers and reboot as described in the guide.
2. Verify the download checksum and run Setup as administrator.
3. Select the public certificate trust task if the test certificate is not already trusted.
4. Connect USB-C, or pair the trackpad through Windows Bluetooth settings. Unplug USB to use Bluetooth.
5. Configure taps, scrolling and multi-finger gestures in Windows **Settings > Bluetooth & devices > Touchpad** (Windows 11) or **Settings > Devices > Touchpad** (Windows 10).

The public signing certificate thumbprint is `94A13BEE28D788FC4A7FA91C568968C7DBADDC69`. No private key is included in downloads.

### Prebuilt driver (no installer)

Download the `x64-driver.zip` or `arm64-driver.zip` asset for your system. Each
contains the compiled drivers, catalogs, INFs, public certificate and manual
instructions. After preparing test signing and trusting the certificate, run
`pnputil /add-driver .\AmtPtpDevice.inf /install` from an administrator terminal
in the extracted directory. The repository also retains a prebuilt
[x64 package](release/x64).

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
| Trackpad icon in Settings | Shown as a touchpad, not a mouse (Windows 11 24H2 and later); confirmed on Bluetooth |
| Bluetooth battery level in Settings | Confirmed on Bluetooth, via the battery service |

v0.2.2 test builds were confirmed over USB and Bluetooth on x64, including the touchpad icon and battery level over Bluetooth. The USB icon and live ARM64 input are not yet checked.

Windows only reads battery levels natively from Bluetooth LE devices and headsets. For this classic Bluetooth trackpad, Setup installs a small service, `AmtPtpBattery`. It asks the trackpad for its battery report through the standard Windows HID API every five minutes and when the trackpad connects, then stores the percentage where Settings › Bluetooth & devices reads it. It runs as LocalSystem with only the basic service privilege, because writing that value needs administrator rights. Uninstall removes it.

Both transports use 125 Hz output, a 12 ms interpolation delay and measured-position history. Contact and button transitions are immediate. Lift-off is reported once at the last delivered position so Windows can preserve momentum. Prediction is bounded; there is no position low-pass filter that changes motion amplitude.

Bluetooth uses device timestamps to preserve buffered packet spacing and retries transient transport errors without restarting the gesture. USB uses a high-resolution native waitable timer. Radio latency and Windows scheduling can still differ.

The Bluetooth filter reads raw reports from the Windows Bluetooth HID transport. HIDCLASS hides that transport's own handler, so the filter calls the handler directly for its own requests through a private kernel-only device. Unlike upstream and v0.2.0, it no longer rewrites the shared Bluetooth HID driver's dispatch table, so other Bluetooth keyboards and mice are unaffected. A reboot clears the old patch after upgrading from v0.2.0 or earlier.

See [USB/Bluetooth parity](docs/transport-parity.md), [Bluetooth implementation and testing](docs/bluetooth-trackpad-3.md), and the [v0.2.2 release notes](docs/release-v0.2.2.md).

## Device support

The package retains upstream USB, SPI, Bluetooth and T2 MacBook device support. Magic Trackpad 2 shares the Type 5 protocol. Hardware testing covered **Magic Trackpad 3 USB-C (product 0324) on x64 Windows**. ARM64 packages passed build, architecture, signature and catalog checks. Live ARM64 input testing remains outstanding.

Legacy device lists and project history are preserved in the [upstream README snapshot](docs/upstream-readme.md). See the [upstream PR review](docs/upstream-pr-review.md) for selected fixes and changes deferred pending compatible hardware.

## Troubleshooting and uninstall

- Pair in Windows settings, then unplug USB when checking Bluetooth.
- If upgrading while disconnected, reconnect the trackpad to select the new package.
- Check Device Manager for a healthy Apple trackpad and Precision Touchpad child device.
- If Setup stops, read its message and logs in `C:\Program Files\Mac Precision Touchpad State`.
- Uninstall through Windows Installed apps. Only packages recorded by this installer are removed, and certificate trust is removed only if Setup added it. Backups and logs remain for recovery.
- Trusting the test certificate lets Windows trust anything signed with its key, not only this driver. See [what certificate trust allows](docs/installer.md#what-certificate-trust-allows) and how to remove it.
- The standalone `AmtPtpHidFilter.inf` in the driver ZIPs is for Bluetooth only. USB trackpads use `AmtPtpDevice.inf`.
- No battery level in Settings: check that the `AmtPtpBattery` service is running (`Get-Service AmtPtpBattery`) and the trackpad is connected over Bluetooth. The level is read every five minutes.

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
.\tests\build-release-x64.ps1 -Platform ARM64
.\installer\Build-Installer.ps1
```

The installer build needs Inno Setup 6.7.3, Windows SignTool and the local signing certificate/private key. It verifies each driver's PE architecture, signatures and catalog membership, signs Setup and its helpers, and generates separate driver-only ZIPs, manifests and checksums. See [installer build details](docs/installer.md#build-the-installer).

The build retains `SkipPackageVerification=true` for legacy INF compatibility; successful builds and test signatures do not imply current InfVerif or WHQL certification.

## License and acknowledgements

USB code is [GPLv2](LICENSE-GPL.md); SPI code is [MIT](LICENSE-MIT.md). See [license details](LICENSE.md). Original implementation by imbushuo and upstream contributors, with protocol work informed by [magictrackpad2-dkms](https://github.com/robbi5/magictrackpad2-dkms) and [macbook12-spi-driver](https://github.com/cb22/macbook12-spi-driver).
