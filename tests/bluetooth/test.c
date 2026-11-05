#include <driver.h>
#include <assert.h>
#include <stdio.h>
#include "../../src/AmtPtpHidFilter/include/Type5Input.h"

static void finger(UCHAR *packet, unsigned slot, unsigned id, int x, int y, unsigned state)
{
    UCHAR *f = packet + 4 + slot * 9;
    unsigned px = (unsigned)x & 8191, py = (unsigned)(-y) & 8191;
    memset(f, 0, 9);
    f[0] = px; f[1] = (px >> 8) | ((py & 7) << 5);
    f[2] = py >> 3; f[3] = ((py >> 11) & 3) | state; f[8] = id;
}
static BOOLEAN build(UCHAR *packet, size_t count, PTP_REPORT *previous, PTP_REPORT *out)
{
    return PtpFilterBuildType5Report(packet, 4 + count * 9, 4,
        -3678, 3934, -2479, 2586, 1234, TRUE, TRUE, previous, out);
}
int main(void)
{
    UCHAR packet[4 + 9 * 16] = { 0x31 };
    PTP_REPORT previous = {0}, out;
    _Static_assert(sizeof(PTP_REPORT) == 50, "PTP report layout");
    assert(PtpFilterScanTicks(100, 10000000, 0) == 1);
    assert(PtpFilterScanTicks(100, 10000000, 1) == 2);
    assert(PtpFilterScanTicks(1000000, 10000000, 2) == 1000);
    assert((USHORT)PtpFilterScanTicks(65536000, 10000000, 65535) == 0);
    assert((USHORT)PtpFilterScanTicks(65536000, 10000000, 65536) == 1);
    packet[1] = 0xc1; // Timestamp bits must not be interpreted as button bits.
    finger(packet, 0, 15, -300, 500, 0x80);
    finger(packet, 1, 2, 300, -500, 0x80);
    assert(build(packet, 2, &previous, &out));
    assert(out.ContactCount == 2 && out.ScanTime == 1234 && out.IsButtonClicked);
    assert(out.Contacts[0].ContactID == 2 && out.Contacts[1].ContactID == 15);
    assert(out.Contacts[1].X == 3378 && out.Contacts[1].Y == 2979);
    assert(out.Contacts[0].TipSwitch && out.Contacts[1].TipSwitch); // Zero area is a valid light touch.
    previous = out;
    finger(packet, 0, 2, -4096, 4095, 0x00); // Bogus lift position; id 15 disappears.
    assert(build(packet, 1, &previous, &out));
    assert(out.ContactCount == 2);
    for (unsigned i = 0; i < 2; i++) {
        assert(!out.Contacts[i].TipSwitch);
        assert(out.Contacts[i].X == previous.Contacts[i].X);
        assert(out.Contacts[i].Y == previous.Contacts[i].Y);
    }
    previous = out;
    assert(build(packet, 1, &previous, &out) && out.ContactCount == 0);
    previous = out;
    finger(packet, 0, 2, 4095, -4095, 0x80);
    assert(build(packet, 1, &previous, &out));
    assert(out.ContactCount == 1 && out.Contacts[0].TipSwitch);
    assert(out.Contacts[0].X == 7612 && out.Contacts[0].Y == 0);
    previous = out;
    assert(PtpFilterBuildType5Report(packet, 13, 4, -3678, 3934, -2479, 2586,
        1240, FALSE, FALSE, &previous, &out));
    assert(!out.Contacts[0].TipSwitch && !out.IsButtonClicked);
    assert(!PtpFilterBuildType5Report(packet, 3, 4, 0, 1, 0, 1, 0, TRUE, TRUE, &previous, &out));
    assert(!PtpFilterBuildType5Report(packet, 5, 4, 0, 1, 0, 1, 0, TRUE, TRUE, &previous, &out));
    memset(&previous, 0, sizeof(previous));
    for(unsigned i = 0; i < 5; i++) finger(packet, i, 15-i, 100, 200, 0x80);
    assert(build(packet, 5, &previous, &out)); previous = out;
    for(unsigned i = 0; i < 5; i++) finger(packet, i+1, 15-i, 100, 200, 0x80);
    finger(packet, 0, 0, 100, 200, 0x80);
    assert(build(packet, 6, &previous, &out));
    assert(out.ContactCount == 5);
    for(unsigned i = 0; i < 5; i++) assert(out.Contacts[i].ContactID >= 11);
    puts("PASS: Bluetooth Type5 decoding, signed coordinates, light touch, button mask, ID ordering/reuse, one-shot lift, missing contacts, selective reporting, malformed length, capacity");
    return 0;
}
