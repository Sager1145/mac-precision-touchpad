# Upstream PR review (2026-11-04)

Reviewed all five currently open PRs in `imbushuo/mac-precision-touchpad`.
Selected individual fixes rather than merging branches over this fork's
tested Magic Trackpad 3 USB/Bluetooth resampling and recovery work.

| PR | Assessment and action |
| --- | --- |
| [573: spelling correction](https://github.com/imbushuo/mac-precision-touchpad/pull/573) | Applied the `manually` correction and repaired the adjacent Trackpad++ installation sentence. |
| [579: Magic Trackpad 1 Bluetooth](https://github.com/imbushuo/mac-precision-touchpad/pull/579) | The report initialization and clock fixes also expose problems in the older USB UMDF handler. Fixed that handler as described below. MT1-specific packet formats, configuration and device IDs remain deferred until MT1 hardware can be tested. |
| [533: MT2 improvements](https://github.com/imbushuo/mac-precision-touchpad/pull/533) | Dedicated touch-state bits, correct performance-counter usage and continued Bluetooth reads after non-touch reports are already addressed by this fork's current USB/Bluetooth work. The Bluetooth resampler also uses device timestamps to retain the spacing of buffered physical reports. |
| [550: INF configurations and MT2 changes](https://github.com/imbushuo/mac-precision-touchpad/pull/550) | The input fixes overlap with 533. Deferred its installer migration: it changes the minimum OS decoration and substitutes WUDFRd sections in the kernel-mode USB installation. This needs a separate compatibility review and installation testing across the supported stacks. |
| [546: sensitivity FAQ](https://github.com/imbushuo/mac-precision-touchpad/pull/546) | Did not copy the FAQ: its answer points a scrolling-sensitivity question to the cursor-speed slider. That does not establish a fix for scrolling sensitivity. |

## Additional fixes in the legacy USB UMDF handler

`AmtPtpServiceTouchInputInterrupt` handles Type 2, 3 and 4 built-in trackpads.
It now:

- Clears the entire report, including unused contacts, reserved bits and
  the contact count when only button reporting is enabled.
- Uses the existing fixed-epoch Scan Time helper instead of repeatedly
  reporting a frame interval capped at 255. Windows expects an advancing
  counter in 100-microsecond units that wraps at 16 bits; see the
  [Microsoft touchpad report specification](https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/touchpad-windows-precision-touchpad-collection#scan-time).
- Rejects truncated headers, partial finger records and missing button bytes
  before dequeuing a Windows read. This prevents length underflow and reads
  beyond the received packet.
- Completes every dequeued read even if retrieving or copying its output
  buffer fails. Previously those requests could remain outstanding indefinitely.

These changes do not alter Type 5 interpolation or the Bluetooth input path.

## Validation

Run from the repository root:

```powershell
.\tests\legacy-usb\run.ps1
.\tests\interpolation\run.ps1
.\tests\bluetooth\run.ps1
.\tests\build-release-x64.ps1
```

All passed on 2026-11-04. The new harness compiles the actual legacy decoder
and Scan Time helper and tests every Type 2/3/4 configuration in the device
table. It covers valid reports, full-report initialization, button-only input,
unequal frame intervals, 16-bit rollover, truncated packets, an empty request
queue and both output-buffer failure paths.

The Release x64 solution rebuild completed with zero warnings and zero errors
using the existing EWDK build script. That script retains
`SkipPackageVerification=true`; this is not a claim that the legacy INFs pass
modern InfVerif. The rebuilt USB DLL has a valid local WDK test signature.
No new hardware installation was performed for these legacy-device fixes;
physical Type 2/3/4 hardware validation remains outstanding.
