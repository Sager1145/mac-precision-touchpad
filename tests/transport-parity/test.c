#include <driver.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../../src/AmtPtpHidFilter/include/Type5Input.h"
#include "../../src/AmtPtpHidFilter/include/Type5Resampling.h"
#define TraceLoggingWrite(...) ((void)0)
void WdfRequestComplete(WDFREQUEST request, NTSTATUS status);
void WdfRequestSetInformation(WDFREQUEST request, ULONG_PTR bytes);
#include "usb-under-test.inc"

struct TEST_REQUEST { PTP_REPORT report; ULONG_PTR bytes; } testRequest;
static LONGLONG now;
static unsigned completed, compared;
BOOL TestQueryPerformanceCounter(LARGE_INTEGER *counter) { counter->QuadPart = now; return TRUE; }
NTSTATUS WdfIoQueueRetrieveNextRequest(WDFQUEUE queue, WDFREQUEST *request) {
    (void)queue; *request = &testRequest; return STATUS_SUCCESS;
}
NTSTATUS WdfRequestRetrieveOutputMemory(WDFREQUEST request, WDFMEMORY *memory) {
    *memory = request; return STATUS_SUCCESS;
}
NTSTATUS WdfMemoryCopyFromBuffer(WDFMEMORY memory, size_t offset, void *buffer, size_t length) {
    (void)offset; memcpy(&memory->report, buffer, length); return STATUS_SUCCESS;
}
void WdfRequestSetInformation(WDFREQUEST request, ULONG_PTR bytes) { request->bytes = bytes; }
void WdfRequestComplete(WDFREQUEST request, NTSTATUS status) {
    assert(request == &testRequest && NT_SUCCESS(status)); completed++;
}
static void compare(const PTP_REPORT *usb, const PTP_REPORT *bt) {
    assert(usb->ReportID == bt->ReportID);
    assert(usb->ContactCount == bt->ContactCount);
    assert(usb->IsButtonClicked == bt->IsButtonClicked);
    for (unsigned i = 0; i < usb->ContactCount; i++) {
        const PTP_CONTACT *a = &usb->Contacts[i], *b = &bt->Contacts[i];
        assert(a->ContactID == b->ContactID && a->TipSwitch == b->TipSwitch && a->Confidence == b->Confidence);
        // USB uses doubles, KMDF uses integers; one device unit is <0.03mm.
        assert(abs((int)a->X - b->X) <= 1 && abs((int)a->Y - b->Y) <= 1);
    }
    compared++;
}
static void encode(UCHAR *f, unsigned id, int x, int y, unsigned state) {
    unsigned px = (unsigned)(x - 3678) & 8191;
    unsigned py = (unsigned)(2479 - y) & 8191;
    memset(f, 0, 9);
    f[0] = (UCHAR)px; f[1] = (UCHAR)((px >> 8) | ((py & 7) << 5));
    f[2] = (UCHAR)(py >> 3); f[3] = (UCHAR)((py >> 11) | state); f[8] = (UCHAR)id;
}
static void deliverBt(PTP_RESAMPLE_STATE *state, PTP_REPORT *previous, DEVICE_CONTEXT *usb, unsigned before) {
    PTP_REPORT sample, normalized;
    PtpFilterResampleBuild(state, now, usb->QpcFrequency, 7612, 5065, &sample);
    PtpFilterNormalizeReport(&sample, previous, &normalized);
    BOOLEAN deliver = normalized.ContactCount || normalized.IsButtonClicked != previous->IsButtonClicked;
    assert(completed - before == (unsigned)deliver);
    if (deliver) {
        compare(&testRequest.report, &normalized);
        *previous = normalized;
    }
}
int main(void) {
    DEVICE_CONTEXT usb = {0};
    PTP_RESAMPLE_STATE bt = {0};
    PTP_REPORT previousPhysical = {0}, previous = {0};
    for (const struct BCM5974_CONFIG *cfg = Bcm5974ConfigTable; cfg->ansi; cfg++)
        if (cfg->ansi == USB_DEVICE_ID_APPLE_MAGICTRACKPAD2_USBC) usb.DeviceInfo = cfg;
    assert(usb.DeviceInfo);
    usb.QpcFrequency = 10000000;
    usb.IsSurfaceReportOn = usb.IsButtonReportOn = TRUE;
    assert(INTERP_PERIOD_MS == PTP_RESAMPLE_PERIOD_MS && INTERP_DELAY_MS == PTP_RESAMPLE_DELAY_MS);
    assert(INTERP_MAX_EXTRAP_MS == PTP_RESAMPLE_EXTRAP_MS && INTERP_HISTORY_COUNT == PTP_RESAMPLE_HISTORY);
    int nextRaw = 0, rawIndex = 0;
    for (int ms = 0; ms <= 900; ms++) {
        now = (LONGLONG)ms * 10000;
        if (ms == nextRaw) {
            UCHAR cable[12 + 16*9] = {0}, radio[4 + 16*9] = {0};
            PTP_REPORT physical;
            // Drag, two-finger scroll, three/five-finger gestures, lift, reuse.
            unsigned count = ms < 150 ? 1 : ms < 330 ? 2 : ms < 450 ? 3 : ms < 570 ? 5 : ms < 640 ? 0 : ms < 790 ? 1 : 0;
            unsigned button = ms < 150;
            cable[1] = (UCHAR)(0xc0 | button); // Other bits never mean click.
            radio[0] = 0x31; radio[1] = (UCHAR)(((ms & 31) << 3) | button);
            radio[2] = (UCHAR)(ms >> 5); radio[3] = (UCHAR)(ms >> 13);
            // Put a near-field finger before the real contacts and reverse
            // record order. Active contacts must survive the five-slot limit.
            unsigned records = count ? count + 1 : 0;
            if (count) encode(cable + 12, 1, 4000, 2500, 0x40);
            for (unsigned i = 0; i < count; i++) {
                unsigned id = 15 - i*2;
                int x = 1000 + (ms < 350 ? ms*3 : 2100-ms*3) + i*70;
                encode(cable + 12 + (count-i)*9, id, x, 1800 + ms/3 + i*30, 0x80);
            }
            memcpy(radio + 4, cable + 12, records*9);
            assert(PtpFilterBuildType5Report(radio, 4+records*9, 4, -3678, 3934, -2479, 2586,
                0, TRUE, TRUE, &previousPhysical, &physical));
            LONGLONG source = PtpFilterSourceTime(&bt, radio, now, usb.QpcFrequency);
            BOOLEAN transition = PtpFilterResampleOnSample(&bt, &physical, source, now, usb.QpcFrequency);
            previousPhysical = physical;
            unsigned before = completed;
            assert(NT_SUCCESS(AmtPtpServiceTouchInputInterruptType5(&usb, cable, 12+records*9)));
            compare(&usb.LastPhysicalReport, &physical);
            if (transition) deliverBt(&bt, &previous, &usb, before);
            else assert(completed == before);
            const int intervals[] = {11, 12, 25, 9};
            nextRaw += intervals[rawIndex++ % 4]; // Also exercise bounded prediction.
        }
        if (ms && ms % 8 == 0) {
            PTP_REPORT output;
            unsigned before = completed;
            AmtPtpInterpBuildReport(&usb, now, &output);
            AmtPtpEmitReport(&usb, &output, now);
            deliverBt(&bt, &previous, &usb, before);
        }
    }
    // A long packet gap must discard stale velocity/history in both paths.
    PTP_REPORT sample = {0}, a, b;
    sample.ContactCount = 1; sample.Contacts[0].ContactID = 15;
    sample.Contacts[0].TipSwitch = sample.Contacts[0].Confidence = TRUE;
    sample.Contacts[0].X = 2000; sample.Contacts[0].Y = 2000;
    LARGE_INTEGER time = {.QuadPart = 10000000};
    AmtPtpInterpOnSample(&usb, &sample, time);
    PtpFilterResampleOnSample(&bt, &sample, time.QuadPart, time.QuadPart, usb.QpcFrequency);
    sample.Contacts[0].X += 100;
    time.QuadPart += 3000000;
    AmtPtpInterpOnSample(&usb, &sample, time);
    PtpFilterResampleOnSample(&bt, &sample, time.QuadPart, time.QuadPart, usb.QpcFrequency);
    AmtPtpInterpBuildReport(&usb, time.QuadPart + 300000, &a);
    PtpFilterResampleBuild(&bt, time.QuadPart + 300000, usb.QpcFrequency, 7612, 5065, &b);
    compare(&a, &b);
    assert(a.Contacts[0].X == 2100 && b.Contacts[0].X == 2100);
    // Malformed USB packets must leave the active interpolation state intact.
    UCHAR shortPacket[12] = {0};
    unsigned before = completed;
    assert(AmtPtpServiceTouchInputInterruptType5(&usb, shortPacket, 11) == STATUS_INVALID_BUFFER_SIZE);
    assert(completed == before && usb.InterpFingers[15].Active);
    assert(compared > 100);
    printf("PASS: %u USB/Bluetooth comparisons: motion/reversals, drag, 2/3/5-finger gestures, contact ordering, click bits, one-shot lift/momentum handoff, ID reuse and gap recovery\n", compared);
    return 0;
}
