# v0.3.2

Bluetooth reliability fixes found in a second round of code reviews of v0.3.1.
Nothing changes over USB, and motion is the same as in v0.3.1.

Installed and checked on x64 with Magic Trackpad 3 over Bluetooth: smooth
circles, the battery level, and reconnecting after the trackpad is switched
off and on. The x64 and ARM64 builds passed. Live ARM64 input remains
unchecked.

## Fixed

- **Faster reconnect.** After the trackpad was off for more than a few
  seconds, v0.3.1 retried switching it to multitouch mode only every 5 s, so
  a trackpad switched back on could take up to about 7 s to respond. The
  retry now backs off to every 1 s instead, so the trackpad works again
  within about 1–3 s.
- **Start failure no longer holds the Bluetooth device.** If the filter
  failed to start (for example, if the trackpad didn't answer its first
  request), it could keep its reference to Windows' Bluetooth HID device and
  its private proxy device. That could keep the device from being removed
  and the driver from unloading. The filter now releases both on every
  start failure.
- **No battery work after power-down.** A battery report that completed
  while the filter was powering down could queue a battery update after the
  filter had already flushed that work. The filter now flushes it only after
  every read has stopped, and drops reads that complete during power-down.
- **An internal start error is reported as an error.** One start-up failure
  path returned success.

## Corrected

- The v0.3.1 notes credited the battery level after a first pair to a filter
  change. That change had no effect, because the filter doesn't read the
  trackpad before multitouch mode is set. The level shows because the legacy
  settings app no longer asks the trackpad for its battery report. The
  ineffective code is removed, and the v0.3.1 notes are corrected.
- The v0.3.1 notes said cancelling a forwarded device-name request helps
  sleep. It helps removal only; sleep was already covered.

## Unchanged

- If Windows' Bluetooth HID driver ever ignored the cancel of the
  multitouch-mode request, sleep could still wait on it. That needs the
  request to be sent asynchronously, which is a larger change.
- Over Bluetooth, nothing asks the trackpad for its battery report.
- Signatures still use the WDK test certificate and carry no timestamp, and
  `SHA256SUMS.txt` is not separately signed.

This release uses WDK test signing. Prepare Windows to load test-signed
drivers and verify your download with `SHA256SUMS.txt`.
[Installation and recovery guide](https://github.com/Sager1145/mac-precision-touchpad/blob/master/docs/installer.md).
