#include <driver.h>
#include <assert.h>
#include <stdio.h>
#include "../../src/AmtPtpHidFilter/include/Type5Resampling.h"

static void stamp(UCHAR* header, ULONG value)
{
    header[0] = 0x31; header[1] = (UCHAR)((value & 31) << 3);
    header[2] = (UCHAR)(value >> 5); header[3] = (UCHAR)(value >> 13);
}
int main(void)
{
    PTP_RESAMPLE_STATE state = {0};
    PTP_REPORT sample = {0}, out, previous = {0}, delivered;
    UCHAR header[4];
    const LONGLONG frequency = 10000000;
    int nextRaw = 0, rawIndex = 0, frames = 0;
    LONG previousX = 0;
    sample.ReportID = REPORTID_MULTITOUCH; sample.ContactCount = 2; sample.IsButtonClicked = 1;
    sample.Contacts[0].ContactID = 2; sample.Contacts[1].ContactID = 15;
    sample.Contacts[0].TipSwitch = sample.Contacts[1].TipSwitch = 1;
    sample.Contacts[0].Confidence = sample.Contacts[1].Confidence = 1;
    for (int ms = 0; ms <= 600; ms++) {
        LONGLONG now = (LONGLONG)ms * 10000;
        if (ms == nextRaw) {
            for (int i = 0; i < 2; i++) {sample.Contacts[i].X = (USHORT)(1000+i*500+ms*2);sample.Contacts[i].Y = (USHORT)(1500+ms*3);}
            stamp(header, (ULONG)ms);
            LONGLONG source = PtpFilterSourceTime(&state, header, now, frequency);
            assert(source == now);
            assert(PtpFilterResampleOnSample(&state, &sample, source, now, frequency) == (ms == 0));
            nextRaw += 11 + (rawIndex++ % 2);
        }
        if (ms % 8 == 0) {
            PtpFilterResampleBuild(&state, now, frequency, 7612, 5065, &out);
            int target = ms-12; if (target < 0) target = 0;
            assert(out.ContactCount == 2);
            assert(out.Contacts[0].X == 1000+target*2);
            assert(out.Contacts[0].Y == 1500+target*3);
            assert(out.Contacts[1].X == 1500+target*2);
            assert(out.Contacts[0].X >= previousX);
            if (ms >= 24) assert(out.Contacts[0].X - previousX == 16);
            previousX = out.Contacts[0].X;
            PtpFilterNormalizeReport(&out, &previous, &delivered);
            previous = delivered; frames++;
        }
    }
    assert(frames == 76); // Startup plus 75 regular 8ms samples.
    assert(state.Fingers[2].Vx >= 1995 && state.Fingers[2].Vx <= 2000);
    sample.ContactCount = 0; sample.IsButtonClicked = 0;
    assert(PtpFilterResampleOnSample(&state, &sample, 6010000, 6010000, frequency));
    PtpFilterResampleBuild(&state, 6010000, frequency, 7612, 5065, &out);
    PtpFilterNormalizeReport(&out, &previous, &delivered);
    assert(delivered.ContactCount == 2 && !delivered.IsButtonClicked);
    for (int i=0;i<2;i++) {
        assert(!delivered.Contacts[i].TipSwitch);
        assert(delivered.Contacts[i].X == previous.Contacts[i].X);
        assert(delivered.Contacts[i].Y == previous.Contacts[i].Y);
    }
    previous = delivered;
    PtpFilterNormalizeReport(&out, &previous, &delivered); assert(delivered.ContactCount == 0);
    // The 21-bit millisecond device counter wraps without resetting sample time.
    RtlZeroMemory(&state, sizeof(state));
    stamp(header, 0x1ffffa);
    assert(PtpFilterSourceTime(&state, header, 1000000, frequency) == 1000000);
    state.SourceTime = 1000000;
    stamp(header, 5);
    assert(PtpFilterSourceTime(&state, header, 1000010, frequency) == 1110000);
    // Buffered Bluetooth packets retain device spacing, not the tiny host-arrival interval.
    state.SourceTime = 1110000;
    stamp(header, 16);
    assert(PtpFilterSourceTime(&state, header, 1000020, frequency) == 1220000);
    puts("PASS: integer Bluetooth resampling, 125 Hz continuous drag/scroll, unchanged velocity, immediate transitions, one-shot lift, device timestamp wrap and buffered packet timing");
    return 0;
}
