#pragma once
#define PTP_RESAMPLE_PERIOD_MS 8
#define PTP_RESAMPLE_DELAY_MS 12
#define PTP_RESAMPLE_EXTRAP_MS 15
#define PTP_RESAMPLE_HISTORY 8

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
    BOOLEAN HasSample, ClockValid;
    UCHAR Button;
    ULONG DeviceTimestamp;
    LONGLONG SourceTime, ArrivalTime;
} PTP_RESAMPLE_STATE;
