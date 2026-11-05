// Integer-only Type 5 translation shared by Bluetooth and HID USB transports.
#pragma once

// A Bluetooth transport may drain buffered frames faster than 100us. Keep
// their unwrapped timestamps strictly increasing while returning to wall time
// as soon as it catches up. Commit this value only after delivery succeeds.
static inline LONGLONG
PtpFilterScanTicks(LONGLONG Elapsed, LONGLONG Frequency, LONGLONG Previous)
{
    LONGLONG ticks = Frequency > 0 && Elapsed >= 0 ?
        (Elapsed / Frequency) * 10000 + (Elapsed % Frequency) * 10000 / Frequency : 0;
    return ticks > Previous ? ticks : Previous + 1;
}

static inline BOOLEAN
PtpFilterBuildType5Report(
    const UCHAR* Buffer, size_t Length, size_t Header,
    INT MinX, INT MaxX, INT MinY, INT MaxY,
    USHORT ScanTime, BOOLEAN TouchOn, BOOLEAN ButtonOn,
    const PTP_REPORT* Previous, PPTP_REPORT Report
)
{
    PTP_CONTACT raw[16] = { 0 };
    BOOLEAN present[16] = { 0 }, included[16] = { 0 };
    size_t count, i;
    if (Header < 2 || Length < Header || (Length - Header) % 9 != 0) return FALSE;
    count = (Length - Header) / 9;
    if (count > 16) return FALSE;
    RtlZeroMemory(Report, sizeof(*Report));
    Report->ReportID = REPORTID_MULTITOUCH;
    Report->ScanTime = ScanTime;
    Report->IsButtonClicked = ButtonOn && ((Buffer[1] & 1) != 0);
    for (i = 0; i < count; i++) {
        const UCHAR* f = Buffer + Header + i * 9;
        ULONG id = f[8] & 15;
        INT x = f[0] | ((f[1] & 31) << 8);
        INT y = (f[1] >> 5) | (f[2] << 3) | ((f[3] & 3) << 11);
        if (x & 4096) x -= 8192;
        if (y & 4096) y -= 8192;
        x -= MinX;
        y = -y - MinY;
        if (x < 0) x = 0;
        if (x > MaxX - MinX) x = MaxX - MinX;
        if (y < 0) y = 0;
        if (y > MaxY - MinY) y = MaxY - MinY;
        raw[id].ContactID = id;
        raw[id].X = (USHORT)x;
        raw[id].Y = (USHORT)y;
        raw[id].TipSwitch = TouchOn && (f[3] & 0xc0) == 0x80;
        raw[id].Confidence = TRUE;
        present[id] = TRUE;
    }
    // Release vanished IDs once at the last delivered down position. Existing
    // IDs have priority over new ones, independent of hardware record order.
    for (i = 0; i < Previous->ContactCount; i++) {
        PTP_CONTACT c = Previous->Contacts[i];
        ULONG id = c.ContactID;
        if (id >= 16 || !c.TipSwitch) continue;
        included[id] = TRUE;
        if (present[id] && raw[id].TipSwitch) c = raw[id];
        else c.TipSwitch = FALSE;
        Report->Contacts[Report->ContactCount++] = c;
    }
    for (i = 0; i < 16 && Report->ContactCount < 5; i++) {
        if (!included[i] && present[i] && raw[i].TipSwitch)
            Report->Contacts[Report->ContactCount++] = raw[i];
    }
    return TRUE;
}
