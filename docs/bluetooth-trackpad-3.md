# Magic Trackpad 3 USB-C over Bluetooth

The discovered device uses Apple Bluetooth vendor 004C, product 0324. Its
mouse collection was previously bound to mouhid and exposed no Precision
Touchpad collection. Both the standalone HID filter INF and universal package
now bind the Bluetooth Col01 interface and the auxiliary Col02 interface.
Runtime product checks and descriptor selection accept 0324 as well as 0265.

## Input correctness and recovery

Bluetooth input uses report 0x31 with a four-byte header and nine-byte finger
records. Coordinates are decoded bytewise and bounded to the descriptor ranges;
the state bits determine contact, including light touches with zero reported
area. Contact IDs remain stable. Lift-off is emitted once at the last delivered
position, whether the device sends an up record or omits the ID.

KeQueryPerformanceCounter returns the counter and writes frequency through its
argument; the old filter used the frequency as the counter. ScanTime now uses
an advancing 16-bit counter in 100us units. Batched input cannot generate
duplicate timestamps. Report padding is zeroed and dequeued requests complete
even on output-buffer errors. The input request context is not read after its
request object is deleted.

An initial hardware trace contained a STATUS_DEVICE_DATA_ERROR transport read,
a three-second recovery delay, and 86 repeated timestamps in 243 output frames.
Transient read errors now retry after 20ms without resetting contact history or
the clock. Repeated errors trigger bounded reconfiguration. Startup/resume
configuration retries after 250ms; the feature request has a two-second timeout.
Mouse-mode input triggers multitouch configuration again. Other non-touch
reports are skipped while continuing the read pump.

Power exit stops both timers and cancels the in-flight transport read. Startup
and resume explicitly kick the pump; they do not rely on a new Windows read
arriving at exactly the right time. An atomic gate limits the transport to one
in-flight read, independent of how many output reads Windows queues.

A paired Bluetooth entry alone does not prove a live connection. During one
test the radio enumerator still showed the paired device, but its live HID
collections were absent and the trace contained no input events. Reconnecting
restored them. Driver-side recovery can act after the HID transport is present;
it does not replace the Bluetooth radio's pairing/link establishment.

## Same interpolation behavior as USB

The KMDF Bluetooth path uses an 8ms high-resolution WDF timer and an eight-point
history with a fixed 12ms interpolation delay. Positions are not low-pass
filtered. A 15Hz integer velocity filter is used only for bounded extrapolation
when the target is newer than available samples. Extrapolation is capped at
15ms. Kernel-mode math is integer-only; no floating-point operations or saved
floating-point processor state are needed.

Continuous physical input updates history. Contact/button transitions bypass
the timer for prompt response, while their surviving contacts remain on the
same resampling timeline. The output normalizer preserves the previously
validated one-shot lift-off semantics used for flick momentum.

The device's 21-bit millisecond timestamp, documented in upstream PR #533, gives
buffered packets their original sample spacing. It is unwrapped independently
of packet arrival; the latest arrival anchors the interpolation target. Output
ScanTime still follows the host clock and stays advancing across soft recovery.

## Upstream review

- [PR #533: MT2 Improvements](https://github.com/imbushuo/mac-precision-touchpad/pull/533)
  identifies non-touch reports starving the read pipeline, contact-state bits,
  and device timestamps. These directly informed packet dispatch and sample
  timing here. It is an open proposal, not a released upstream fix.
- [PR #550](https://github.com/imbushuo/mac-precision-touchpad/pull/550) builds on
  #533 and adjusts INF/build packaging. It does not supply a distinct reconnect
  algorithm.
- [PR #399](https://github.com/imbushuo/mac-precision-touchpad/pull/399) introduced
  the Bluetooth filter and explicitly noted pairing/recovery rough edges.
- [PR #579](https://github.com/imbushuo/mac-precision-touchpad/pull/579) independently
  identifies the counter/frequency and report-padding issues. Its MT1-only packet
  formats are not applied to the USB-C trackpad.
- [Issue #477](https://github.com/imbushuo/mac-precision-touchpad/issues/477) and
  [issue #543](https://github.com/imbushuo/mac-precision-touchpad/issues/543) report
  delayed or reboot-dependent reconnects. Firmware/pairing workarounds in their
  comments are anecdotes; this change does not update firmware or assume those
  procedures solve this device's link behavior.

## Verification

Run `tests/bluetooth/run.ps1` for packet, device-clock, and integer resampling
tests. They cover constant motion at 125Hz from 11/12ms input, preserved velocity,
click-dragging, two fingers, one-shot release, timestamp wrap, and batched input.
The USB regression suite is `tests/interpolation/run.ps1`.

The Release x64 build completed with zero warnings/errors. The filter binary
and catalog are signed with the local WDK test certificate, thumbprint
94A13BEE28D788FC4A7FA91C568968C7DBADDC69, and their signatures and catalog membership
were verified. This is test-signing, not Microsoft/WHQL production signing.

Installed filter, auxiliary, Precision Touchpad, and input-configuration devices
report Status=OK and ProblemCode=0. The user confirmed smooth motion, preserved
momentum, and two successful off/on reconnects with the interpolated build.
Local ETW data is under artifacts/bluetooth and is not committed.

The final 65-second diagnostic window included 3002 output reports from the
interpolated build, of which 2938 were timer frames. Continuous spacing was
8.031 ms median and 8.5154 ms at the 95th percentile. The maximum was 154.6614 ms,
so this is not a claim of perfect scheduling or removal of radio/link stalls.
No continuous interval was below 1 ms. The output had no repeated/backwards
ScanTime values within a power session, missing releases, or invalid release
coordinates. The observed maximum number of in-flight transport reads was one.
The trace still reported transient STATUS_DEVICE_DATA_ERROR failures; these
were retried without resetting the gesture clock/contact history.
