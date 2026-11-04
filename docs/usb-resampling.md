# Magic Trackpad USB resampling fix

The Type 5 input path resamples continuous movement on an 8 ms high-resolution
waitable timer and a dedicated UMDF worker. A normal WDF timer remains as a
fallback for systems where high-resolution waitable timers are unavailable.
Each contact retains eight unfiltered physical positions and acquisition times.
The output position is linearly interpolated at a fixed 12 ms delay, so ordinary
11-12 ms physical sampling leaves measured points on both sides of the target.
This is a fixed latency, not a position low-pass filter: constant motion retains
its original velocity and amplitude. A 15 Hz velocity filter and extrapolation
limited to 15 ms are used only when the target is newer than available samples.
Output coordinates are rounded and clamped to HID bounds.

Contact down/up and button changes bypass the cadence for immediate response;
continuous physical samples only update history. Surviving contacts in a
transition frame use the same resampling timeline to avoid snapping to raw
positions and back. New contacts begin at their measured down position.
The timer and USB callbacks share an explicit lock. Resume clears contact and
velocity state, and contact re-entry starts with zero velocity.
The worker is signaled and joined on D0 exit before its timer/event/thread
handles are closed. The native timer does not request a global clock-resolution
change. Start, stop, restart, cleanup, and failed-native-start fallback are
covered by a test that runs the actual clock helpers against Windows timers.

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
mock WDF reads. Coverage includes unchanged stored raw positions, linear
history interpolation, velocity fallback, the 15 ms cap, advancing ScanTime,
single releases, disappeared IDs, contact reuse, and unavailable reads.
A 600 ms two-finger click-drag simulation feeds alternating 11/12 ms physical
reports and verifies 75 evenly spaced 8 ms output frames with exact constant
motion, no backwards steps, and unchanged release coordinates.
These tests do not exercise WDF timer scheduling or physical gesture recognition.

Run `tests/build-release-x64.ps1 -EwdkRoot D:\BuildEnv` with Enterprise WDK 28000.
This matches the hardware-tested build environment, including the existing
SkipPackageVerification option used by the local build script. It is a test
build, not WHQL/package-compliance validation. Output is test-signed according
to the existing project settings. Logs are written under artifacts/build.

The prior prediction/raw build on 2026-11-04 compiled with zero warnings and errors,
installed with Status=OK and ProblemCode=0, and its installed USB DLL hash
matched the package. The user confirmed smooth touch/drag/scroll movement and
two-finger flick momentum after lifting both fingers.

A 40-second diagnostic capture contained 1785 physical frames, 2131 delivered
frames, and 442 timer frames, with no unavailable reads, duplicate timestamps,
missing release transitions, or incorrect release coordinates. Typical physical
packet spacing was 11-12 ms. Timer/raw corrections still occur: this is bounded
prediction with a nominal 125 Hz timer, not a guaranteed uniform 125 Hz stream.
Raw diagnostic captures and machine-specific reinstall scripts are kept local.

## Continuous-motion candidate

The fixed-history candidate builds with zero errors/warnings. The first WDF-only
candidate's measured continuous cadence was approximately 15.6 ms, so it was
replaced by a high-resolution waitable-timer worker. The native 8 ms timer is
a requested cadence, not a hard real-time guarantee.
The documented high-resolution WDF timer flag is KMDF-only and is deliberately
not set in this UMDF driver; see Microsoft's
[timer configuration reference](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdftimer/ns-wdftimer-_wdf_timer_config).
High-resolution waitable timers are supported from Windows 10 version 1803;
see [CreateWaitableTimerExW](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-createwaitabletimerexw).

The final candidate installed with Status=OK and ProblemCode=0. A 45-second
hardware capture recorded 912 physical frames and 1148 delivered reports,
including 1094 timer frames. Continuous report spacing was 8.1563 ms median,
8.5543 ms at the 95th percentile, and 15.0602 ms maximum. There were no continuous
intervals below 1 ms, no unavailable reads, no duplicate ScanTime values, no
missing releases, and no invalid lift-off coordinates. Nine continuous intervals
exceeded 12 ms, so this is not a claim of perfect real-time scheduling.
The user confirmed the result is smoother and flick momentum still works.
