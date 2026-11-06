# v0.3.1

Smoother Bluetooth motion and Bluetooth stability fixes. Nothing changes over
USB.

Installed and checked on x64 with Magic Trackpad 3 over Bluetooth and USB:
drawing circles over Bluetooth is now smooth, and the battery level shows
after a fresh pair. The x64 and ARM64 builds passed. Live ARM64 input remains
unchecked.

## Smoother Bluetooth motion

Drawing circles over Bluetooth came out uneven and slightly polygonal. Three
causes, three fixes:

- **Playback on the trackpad's own clock.** Output was timed relative to
  each packet's arrival. Bluetooth delivers packets in bursts, so the
  playback speed jumped with every packet. The filter now maps the trackpad's
  timestamps onto the PC clock using the lowest delay seen (a packet can only
  be late, never early) and plays back on that clock.
- **Curves instead of straight lines.** Between two samples the filter drew a
  straight line, so a circle became a polygon. It now interpolates with a
  cubic curve (non-uniform Catmull-Rom) through the neighbouring samples.
- **A delay that fits the link.** The fixed 12 ms delay often ran past the
  newest sample, so the filter had to guess ahead and then correct. The delay
  now adapts: one sample interval plus how late packets typically arrive, plus
  2 ms, kept between 10 and 28 ms. It rises within a few packets and decays
  over a few seconds, so playback never stalls or jumps. On a steady link this
  is about the same latency as before.

USB keeps its own resampler and its 12 ms delay.

## Bluetooth fixes

- **Multitouch-mode request built the way Windows builds it.** At start and
  after a reconnect, the filter sends the trackpad a feature report (0xF1) to
  switch it into multitouch mode, straight to Windows' Bluetooth HID driver.
  That request now has the layout Windows' HID class driver gives one: the
  report in its own nonpaged buffer with an MDL describing it, instead of no
  MDL and a buffer on the stack. A report request without an MDL is the same
  defect as the `hidbth!HidBthGetReportFlowCompletion` crash (bugcheck 0xD1,
  a read of a NULL MDL) fixed in v0.3.0.
- **No more hammering a disconnected trackpad.** After the Bluetooth link
  dropped, the filter retried reads every 20 ms and the multitouch request
  every 500 ms, forever. Disconnect errors now pause reads for 2 s, and the
  multitouch request backs off to every 5 s after eight attempts. The
  trackpad still reconnects by itself.
- **Removal no longer waits on an unresponsive trackpad.** A device-name
  request forwarded to the trackpad could hold up removal until Windows timed
  it out. The filter now cancels it when the device is removed. (Sleep was
  already covered: the filter cancels its transport requests on power-down.)
- **Battery level after a first pair.** The legacy settings app asked the
  trackpad for its battery report over Bluetooth; it no longer does, and after
  a first pair the level now shows. The filter also gained a change meant to
  keep battery reports that arrive before multitouch mode is set, but the
  filter doesn't read the trackpad until then, so that change had no effect.
  It is removed after v0.3.1.

## Checked

- Six independent code reviews of these changes; the one regression they
  found (the multitouch request's buffer lengths) was fixed before release.

## Unchanged

- Over Bluetooth, nothing asks the trackpad for its battery report; the level
  comes only from reports it sends on its own.
- Signatures still use the WDK test certificate and carry no timestamp, and
  `SHA256SUMS.txt` is not separately signed.

This release uses WDK test signing. Prepare Windows to load test-signed
drivers and verify your download with `SHA256SUMS.txt`.
[Installation and recovery guide](https://github.com/Sager1145/mac-precision-touchpad/blob/master/docs/installer.md).
