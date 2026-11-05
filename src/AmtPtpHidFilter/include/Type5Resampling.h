#pragma once
#include "ResamplingTypes.h"

static LONGLONG
PtpFilterSourceTime(PTP_RESAMPLE_STATE* State, const UCHAR* Header, LONGLONG Now, LONGLONG Frequency)
{
    // PR #533: low five timestamp bits occupy header byte 1 bits 3-7;
    // the following little-endian word is the high part. Units are milliseconds.
    ULONG stamp = (Header[1] >> 3) | ((ULONG)Header[2] << 5) | ((ULONG)Header[3] << 13);
    ULONG delta = (stamp - State->DeviceTimestamp) & 0x1fffff;
    LONGLONG source = !State->ClockValid || delta > 1000 ? Now :
        State->SourceTime + (LONGLONG)delta * Frequency / 1000;
    State->ClockValid = TRUE;
    State->DeviceTimestamp = stamp;
    return source;
}

static LONG
PtpFilterVelocity(LONG Old, LONG Delta, LONGLONG Dt, LONGLONG Frequency)
{
    LONGLONG instant = (LONGLONG)Delta * Frequency / Dt;
    LONGLONG tau = Frequency * 10610 / 1000000; // 1/(2*pi*15 Hz).
    LONGLONG alpha = Dt * 65536 / (tau + Dt);
    if (instant > 1000000) instant = 1000000;
    if (instant < -1000000) instant = -1000000;
    return Old + (LONG)((instant - Old) * alpha / 65536);
}

static BOOLEAN
PtpFilterResampleOnSample(PTP_RESAMPLE_STATE* State, const PTP_REPORT* Sample,
    LONGLONG Source, LONGLONG Arrival, LONGLONG Frequency)
{
    ULONG before = 0, after = 0;
    BOOLEAN seen[16] = {0};
    if (State->HasSample && (Source < State->SourceTime ||
        Arrival - State->ArrivalTime >= Frequency * PTP_RESAMPLE_STALE_MS / 1000)) {
        RtlZeroMemory(State->Fingers, sizeof(State->Fingers));
        State->HasSample = State->HasTarget = FALSE;
    }
    BOOLEAN transition = !State->HasSample || State->Button != Sample->IsButtonClicked;
    ULONG i;
    for (i = 0; i < 16; i++)
        if (State->Fingers[i].Active && State->Fingers[i].Contact.TipSwitch) before |= 1UL << i;
    for (i = 0; i < Sample->ContactCount; i++) {
        PTP_CONTACT c = Sample->Contacts[i];
        if (c.ContactID >= 16) continue;
        PTP_RESAMPLE_FINGER* f = &State->Fingers[c.ContactID];
        LONGLONG dt = f->Count ? Source - f->History[f->Count - 1].Time : 0;
        seen[c.ContactID] = TRUE;
        if (c.TipSwitch) after |= 1UL << c.ContactID;
        if (f->Active && f->Contact.TipSwitch && c.TipSwitch && dt > 0 && dt < Frequency / 5) {
            f->Vx = PtpFilterVelocity(f->Vx, (LONG)c.X - f->Contact.X, dt, Frequency);
            f->Vy = PtpFilterVelocity(f->Vy, (LONG)c.Y - f->Contact.Y, dt, Frequency);
        } else {
            f->Vx = f->Vy = 0;
            if (!f->Active || !f->Contact.TipSwitch || !c.TipSwitch || dt < 0 || dt >= Frequency / 5)
                f->Count = 0;
        }
        if (c.TipSwitch) {
            if (f->Count && f->History[f->Count - 1].Time == Source) f->Count--;
            if (f->Count == PTP_RESAMPLE_HISTORY) {
                RtlMoveMemory(&f->History[0], &f->History[1],
                    (PTP_RESAMPLE_HISTORY - 1) * sizeof(PTP_HISTORY_POINT));
                f->Count--;
            }
            PTP_HISTORY_POINT* p = &f->History[f->Count++];
            p->X = c.X; p->Y = c.Y; p->Time = Source;
        }
        f->Contact = c;
        f->Active = TRUE;
    }
    for (i = 0; i < 16; i++) if (!seen[i]) { State->Fingers[i].Active = FALSE; State->Fingers[i].Count = 0; }
    State->Button = Sample->IsButtonClicked;
    State->SourceTime = Source;
    State->ArrivalTime = Arrival;
    State->HasSample = TRUE;
    return transition || before != after;
}

static VOID
PtpFilterResamplePosition(const PTP_RESAMPLE_FINGER* Finger, LONGLONG Target,
    LONGLONG Frequency, LONG* X, LONG* Y)
{
    ULONG i;
    *X = Finger->Contact.X; *Y = Finger->Contact.Y;
    if (!Finger->Contact.TipSwitch || !Finger->Count) return;
    const PTP_HISTORY_POINT* first = &Finger->History[0];
    if (Target <= first->Time) { *X = first->X; *Y = first->Y; return; }
    for (i = 1; i < Finger->Count; i++) {
        const PTP_HISTORY_POINT* a = &Finger->History[i - 1];
        const PTP_HISTORY_POINT* b = &Finger->History[i];
        if (Target <= b->Time) {
            LONGLONG den = b->Time - a->Time, t = Target - a->Time;
            *X = (LONG)(((LONGLONG)a->X * (den - t) + (LONGLONG)b->X * t + den / 2) / den);
            *Y = (LONG)(((LONGLONG)a->Y * (den - t) + (LONGLONG)b->Y * t + den / 2) / den);
            return;
        }
    }
    const PTP_HISTORY_POINT* last = &Finger->History[Finger->Count - 1];
    LONGLONG dt = Target - last->Time, cap = Frequency * PTP_RESAMPLE_EXTRAP_MS / 1000;
    if (dt > cap) dt = cap;
    // Round the complete nonnegative coordinate, as the USB resampler does.
    // Rounding the displacement alone biases negative movement toward zero.
    *X = (LONG)(((LONGLONG)last->X * Frequency + (LONGLONG)Finger->Vx * dt + Frequency / 2) / Frequency);
    *Y = (LONG)(((LONGLONG)last->Y * Frequency + (LONGLONG)Finger->Vy * dt + Frequency / 2) / Frequency);
}

static VOID
PtpFilterResampleBuild(PTP_RESAMPLE_STATE* State, LONGLONG Now, LONGLONG Frequency,
    LONG MaxX, LONG MaxY, PPTP_REPORT Report)
{
    ULONG i;
    RtlZeroMemory(Report, sizeof(*Report));
    Report->ReportID = REPORTID_MULTITOUCH;
    if (!State->HasSample || Frequency <= 0 ||
        Now - State->ArrivalTime >= Frequency * PTP_RESAMPLE_STALE_MS / 1000) return;
    Report->IsButtonClicked = State->Button;
    LONGLONG target = State->SourceTime + (Now - State->ArrivalTime) - Frequency * PTP_RESAMPLE_DELAY_MS / 1000;
    LONGLONG horizon = State->SourceTime + Frequency * PTP_RESAMPLE_EXTRAP_MS / 1000;
    if (target > horizon) target = horizon;
    if (State->HasTarget && target < State->LastTarget) target = State->LastTarget;
    State->LastTarget = target;
    State->HasTarget = TRUE;
    for (i = 0; i < 16 && Report->ContactCount < 5; i++) {
        const PTP_RESAMPLE_FINGER* f = &State->Fingers[i];
        LONG x, y;
        if (!f->Active) continue;
        PtpFilterResamplePosition(f, target, Frequency, &x, &y);
        if (x < 0) x = 0;
        if (x > MaxX) x = MaxX;
        if (y < 0) y = 0;
        if (y > MaxY) y = MaxY;
        PTP_CONTACT* c = &Report->Contacts[Report->ContactCount++];
        *c = f->Contact; c->X = (USHORT)x; c->Y = (USHORT)y;
    }
}

static VOID
PtpFilterNormalizeReport(const PTP_REPORT* Sample, const PTP_REPORT* Previous, PPTP_REPORT Report)
{
    BOOLEAN included[16] = {0};
    ULONG i, j;
    RtlZeroMemory(Report, sizeof(*Report));
    Report->ReportID = REPORTID_MULTITOUCH;
    Report->IsButtonClicked = Sample->IsButtonClicked;
    for (i = 0; i < Previous->ContactCount; i++) {
        PTP_CONTACT c = Previous->Contacts[i];
        if (!c.TipSwitch || c.ContactID >= 16) continue;
        included[c.ContactID] = TRUE;
        for (j = 0; j < Sample->ContactCount; j++) if (Sample->Contacts[j].ContactID == c.ContactID) break;
        if (j < Sample->ContactCount && Sample->Contacts[j].TipSwitch) c = Sample->Contacts[j];
        else c.TipSwitch = FALSE;
        Report->Contacts[Report->ContactCount++] = c;
    }
    for (i = 0; i < Sample->ContactCount && Report->ContactCount < 5; i++) {
        PTP_CONTACT c = Sample->Contacts[i];
        if (c.ContactID < 16 && !included[c.ContactID] && c.TipSwitch) {
            included[c.ContactID] = TRUE;
            Report->Contacts[Report->ContactCount++] = c;
        }
    }
}
