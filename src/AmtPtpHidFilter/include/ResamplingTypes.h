#pragma once
#define PTP_RESAMPLE_PERIOD_MS 8
// Playback delay behind the trackpad's own clock: one sample interval plus how
// late Bluetooth packets tend to arrive, so the target nearly always lies
// between two real samples, within these bounds.
#define PTP_RESAMPLE_DELAY_MIN_MS 10
#define PTP_RESAMPLE_DELAY_MAX_MS 28
#define PTP_RESAMPLE_EXTRAP_MS 15
#define PTP_RESAMPLE_HISTORY 8
#define PTP_RESAMPLE_STALE_MS 250

typedef struct _PTP_HISTORY_POINT {
    LONG X, Y;
    LONGLONG Time;
} PTP_HISTORY_POINT;

typedef struct _PTP_RESAMPLE_FINGER {
    PTP_CONTACT Contact;
    BOOLEAN Active;
    ULONG Count;
    LONG Vx, Vy; // Integer coordinates/second; no kernel floating point.
    PTP_HISTORY_POINT History[PTP_RESAMPLE_HISTORY];
} PTP_RESAMPLE_FINGER;

typedef struct _PTP_RESAMPLE_STATE {
    PTP_RESAMPLE_FINGER Fingers[16];
    BOOLEAN HasSample, ClockValid, HasTarget;
    UCHAR Button;
    ULONG DeviceTimestamp;
    LONGLONG SourceTime, ArrivalTime;
    LONGLONG LastTarget;
    // Arrival minus source time, tracked at its minimum: Bluetooth only ever
    // delivers late (bursts), so the minimum is the true link latency. Playback
    // runs on Now - ClockOffset, at the trackpad's own pace, not on arrivals.
    LONGLONG ClockOffset;
    BOOLEAN HasOffset;
    LONGLONG Interval; // smoothed sample interval, QPC ticks (0 = unknown)
    LONGLONG Jitter;   // how late packets arrive beyond the minimum: rises quickly, decays slowly
} PTP_RESAMPLE_STATE;
