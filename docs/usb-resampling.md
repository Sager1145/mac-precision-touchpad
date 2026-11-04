# Magic Trackpad USB resampling fix

The Type 5 input path keeps physical positions immediate and applies a 15 Hz
low-pass filter only to velocity. An 8 ms WDF timer predicts between samples,
with extrapolation limited to 15 ms and coordinates clamped to HID bounds.
The timer and USB callbacks share an explicit lock. Resume clears contact and
velocity state, and contact re-entry starts with zero velocity.

ScanTime is an advancing, wrapping 16-bit counter in 100 microsecond units
from a fixed D0 origin. LastEmitTime gates timer ticks; it is not the timestamp
origin. Reporting each interval as ScanTime caused repeated/non-monotonic
timestamps. Contacts now receive exactly one lift-off at their last delivered
position, including when an ID disappears; repeated up/idle frames are suppressed.
Dequeued requests are completed on output-memory errors.

TraceLogging events PhysicalInput, DeliveredInput, and InputReadUnavailable use
the existing provider 871B1E2D-CC5A-4ADE-B74E-6CF1004EF149. Raw packets and output
reports are included when tracing is enabled.

## Validation

Run `tests/interpolation/run.ps1` on Windows with MinGW GCC. It extracts the
production helpers and compiles them with the real report/context headers and
mock WDF reads. Coverage includes unchanged raw positions, velocity filtering,
8 ms prediction, the 15 ms cap, advancing ScanTime, lift-off after prediction,
single releases, disappeared IDs, contact reuse, and unavailable reads.
These tests do not exercise WDF timer scheduling or physical gesture recognition.

Run `tests/build-release-x64.ps1 -EwdkRoot D:\BuildEnv` with Enterprise WDK 28000.
This matches the hardware-tested build environment, including the existing
SkipPackageVerification option used by the local build script. It is a test
build, not WHQL/package-compliance validation. Output is test-signed according
to the existing project settings. Logs are written under artifacts/build.

On 2026-11-04, the corrected build compiled with zero warnings and errors,
installed with Status=OK and ProblemCode=0, and its installed USB DLL hash
matched the package. The user confirmed smooth touch/drag/scroll movement and
two-finger flick momentum after lifting both fingers.

A 40-second diagnostic capture contained 1785 physical frames, 2131 delivered
frames, and 442 timer frames, with no unavailable reads, duplicate timestamps,
missing release transitions, or incorrect release coordinates. Typical physical
packet spacing was 11-12 ms. Timer/raw corrections still occur: this is bounded
prediction with a nominal 125 Hz timer, not a guaranteed uniform 125 Hz stream.
Raw diagnostic captures and machine-specific reinstall scripts are kept local.
