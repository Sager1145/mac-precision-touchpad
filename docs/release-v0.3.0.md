# v0.3.0

The trackpad's battery level now shows in Windows Settings over USB too, and
a crash risk over Bluetooth is removed. This release also adds Magic
Trackpad 2 (05AC:0265) to the USB battery support.

Installed and checked on x64 with Magic Trackpad 3 over USB, including the
battery level in Settings › Bluetooth & devices. The x64 and ARM64 builds
passed. Live ARM64 input remains unchecked.

## Battery

- **USB battery level.** The trackpad's battery report (0x90) is on USB
  interface 0, whose HID report descriptor Windows rejects (hidusb fails with
  Code 10). Earlier versions bound that interface to a null driver, so
  nothing could read it. The driver package now binds it to WinUSB (Magic
  Trackpad 3 0324 and Magic Trackpad 2 0265). The `AmtPtpBattery` service
  asks for the report with a HID class GET_REPORT every five minutes and when
  the trackpad is plugged in, and writes the level to the trackpad's USB
  entry in Settings.
- **No get-report requests over Bluetooth.** Up to v0.2.2 the battery service
  asked a Bluetooth trackpad for its battery report every five minutes with
  `HidD_GetInputReport`. A get-report request that reaches Windows' Bluetooth
  HID driver can crash Windows in
  `hidbth!HidBthGetReportFlowCompletion`: it dereferences a missing MDL. This
  crash was seen with the same request from another app. The service no
  longer touches Bluetooth devices. The Bluetooth filter still shows the
  level from the battery reports the trackpad sends on its own.

## Unchanged

- The Bluetooth filter still sends the multitouch-mode feature report (0xF1)
  directly to the Bluetooth HID transport at start. The trackpad needs it.
- Signatures still use the WDK test certificate and carry no timestamp, and
  `SHA256SUMS.txt` is not separately signed.

This release uses WDK test signing. Prepare Windows to load test-signed
drivers and verify your download with `SHA256SUMS.txt`.
[Installation and recovery guide](https://github.com/Sager1145/mac-precision-touchpad/blob/master/docs/installer.md).
