# Installing this fork's test-signed x64 driver

The prerelease package is signed with a self-signed WDK test certificate.
It is not Microsoft/WHQL/attestation production-signed. It is intended for
Windows x64 test machines configured to load test-signed drivers; it is not
a normal Secure Boot production installation package.

Download the ZIP and matching SHA256SUMS.txt from this fork's GitHub release.
Verify the ZIP's hash before extracting it:

```powershell
Get-FileHash .\mac-precision-touchpad-v0.1.0-preview.1-x64-test-signed.zip -Algorithm SHA256
```

The archive includes the INF files, signed driver binaries and catalogs,
the public test certificate (no private key), a manifest, and per-file hashes.
Keep the signed files intact: modifying an INF or binary invalidates its
catalog membership. The repository's source archive is not the install package.

## Prepare the test machine

Windows must be configured to load test-signed code. Follow Microsoft's
[TESTSIGNING documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option).
Enabling test mode requires administrator rights and a reboot. Secure Boot
policy may prevent enabling it. These instructions do not automatically change
boot, Secure Boot, BitLocker, or Memory Integrity settings.

After reviewing the certificate, install its public certificate in the test
machine's trust stores from an Administrator PowerShell in the extracted folder:

```powershell
certutil -dump .\AmtPtpDeviceTest.cer
certutil -addstore Root .\AmtPtpDeviceTest.cer
certutil -addstore TrustedPublisher .\AmtPtpDeviceTest.cer
```

The expected SHA-1 certificate thumbprint for v0.1.0-preview.1 is:

```text
94A13BEE28D788FC4A7FA91C568968C7DBADDC69
```

For details, see Microsoft's
[test certificate installation guidance](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/installing-test-certificates).

## Install

Back up the currently installed driver package before replacing it. To install
or update matching devices, run from the extracted folder in Administrator PowerShell:

```powershell
pnputil /add-driver .\AmtPtpDevice.inf /install
```

Check the Apple USB Precision Touchpad device in Device Manager. It should
report no problem, with a working device status. Reconnect or restart the
device if Windows requests it. Check continuous pointing, click-dragging,
two-finger scrolling, and flick-and-release momentum before relying on it.

This release was hardware-tested on Magic Trackpad 3 USB-C (05AC:0324).
The package also contains the repository's other x64 drivers, but their
hardware behavior was not independently validated for this release. No ARM64
binary package is included.
