#include <driver.h>
#include "../../src/AmtPtpHidFilter/include/Type5Input.h"
#include <assert.h>
#include <stdio.h>

void WdfRequestComplete(WDFREQUEST request, NTSTATUS status);
void WdfRequestSetInformation(WDFREQUEST request, ULONG_PTR bytes);
#include "input-under-test.inc"

struct TEST_REQUEST { PTP_REPORT report; ULONG_PTR bytes; } testRequest;
static int dequeued, completed, failure;
static NTSTATUS completionStatus;
static LONGLONG now;

BOOL TestQueryPerformanceCounter(LARGE_INTEGER *counter) {
    counter->QuadPart = now;
    return TRUE;
}
NTSTATUS WdfIoQueueRetrieveNextRequest(WDFQUEUE queue, WDFREQUEST *request) {
    (void)queue;
    if (failure == 1) return STATUS_NO_MORE_ENTRIES;
    *request = &testRequest;
    dequeued++;
    return STATUS_SUCCESS;
}
NTSTATUS WdfRequestRetrieveOutputMemory(WDFREQUEST request, WDFMEMORY *memory) {
    if (failure == 2) return STATUS_INVALID_BUFFER_SIZE;
    *memory = request;
    return STATUS_SUCCESS;
}
NTSTATUS WdfMemoryCopyFromBuffer(WDFMEMORY memory, size_t offset, void *buffer, size_t length) {
    (void)offset;
    if (failure == 3) return STATUS_INVALID_BUFFER_SIZE;
    assert(length == sizeof(memory->report));
    memcpy(&memory->report, buffer, length);
    return STATUS_SUCCESS;
}
void WdfRequestSetInformation(WDFREQUEST request, ULONG_PTR bytes) { request->bytes = bytes; }
void WdfRequestComplete(WDFREQUEST request, NTSTATUS status) {
    assert(request == &testRequest);
    completed++;
    completionStatus = status;
}
static void reset(int injectedFailure) {
    memset(&testRequest, 0xa5, sizeof(testRequest));
    testRequest.bytes = 0;
    dequeued = completed = 0;
    failure = injectedFailure;
}
static void testDevice(const struct BCM5974_CONFIG *cfg) {
    DEVICE_CONTEXT ctx = {0};
    UCHAR packet[1024] = {0};
    struct TRACKPAD_FINGER finger = {0};
    PTP_REPORT expected = {0};
    ctx.DeviceInfo = cfg;
    ctx.IsSurfaceReportOn = ctx.IsButtonReportOn = TRUE;
    ctx.QpcFrequency = 10000000;
    ctx.PerfCounter.QuadPart = 1000;
    size_t length = cfg->tp_header + cfg->tp_fsize;
    finger.abs_x = (USHORT)(cfg->x.min + 100);
    finger.abs_y = (USHORT)(cfg->y.max - 200);
    finger.touch_major = 120;
    finger.touch_minor = 60;
    memcpy(packet + cfg->tp_header + cfg->tp_delta, &finger, sizeof(finger));
    packet[cfg->tp_button] = 1;
    expected.ReportID = REPORTID_MULTITOUCH;
    expected.ContactCount = 1;
    expected.IsButtonClicked = TRUE;
    expected.Contacts[0].X = 100;
    expected.Contacts[0].Y = 200;
    expected.Contacts[0].TipSwitch = TRUE;
    expected.Contacts[0].Confidence = TRUE;

    // Fixed epoch, unequal intervals, and rollover must all retain elapsed time.
    const unsigned ticks[] = {100, 180, 400, 65530, 65540};
    for (unsigned i = 0; i < sizeof(ticks)/sizeof(ticks[0]); i++) {
        reset(0);
        now = 1000 + (LONGLONG)ticks[i] * 1000;
        expected.ScanTime = (USHORT)ticks[i];
        assert(NT_SUCCESS(AmtPtpServiceTouchInputInterrupt(&ctx, packet, length)));
        assert(dequeued == 1 && completed == 1 && NT_SUCCESS(completionStatus));
        assert(testRequest.bytes == sizeof(PTP_REPORT));
        // Also check all reserved bits and unused contacts, not only active fields.
        assert(memcmp(&testRequest.report, &expected, sizeof(expected)) == 0);
        assert(ctx.PerfCounter.QuadPart == 1000);
    }
    // Reject short headers, partial fingers and missing button bytes without
    // consuming a Windows read; the next valid packet can still satisfy it.
    const size_t badLengths[] = {0, (size_t)cfg->tp_header - 1, length - 1};
    for (unsigned i = 0; i < sizeof(badLengths)/sizeof(badLengths[0]); i++) {
        reset(0);
        assert(AmtPtpServiceTouchInputInterrupt(&ctx, packet, badLengths[i]) == STATUS_INVALID_BUFFER_SIZE);
        assert(dequeued == 0 && completed == 0);
    }
    ctx.IsSurfaceReportOn = FALSE;
    reset(0);
    assert(AmtPtpServiceTouchInputInterrupt(&ctx, packet, cfg->tp_button) == STATUS_INVALID_BUFFER_SIZE);
    assert(dequeued == 0 && completed == 0);
    reset(0);
    assert(NT_SUCCESS(AmtPtpServiceTouchInputInterrupt(&ctx, packet, cfg->tp_button + 1)));
    assert(testRequest.report.ContactCount == 0 && testRequest.report.IsButtonClicked);
    PTP_REPORT empty = {0};
    empty.ReportID = REPORTID_MULTITOUCH;
    empty.ScanTime = (USHORT)ticks[4];
    empty.IsButtonClicked = TRUE;
    assert(memcmp(&empty, &testRequest.report, sizeof(empty)) == 0);
    ctx.IsSurfaceReportOn = TRUE;
    for (int error = 1; error <= 3; error++) {
        reset(error);
        assert(!NT_SUCCESS(AmtPtpServiceTouchInputInterrupt(&ctx, packet, length)));
        assert(dequeued == (error != 1) && completed == dequeued);
        if (completed) assert(completionStatus == STATUS_INVALID_BUFFER_SIZE);
        assert(testRequest.bytes == 0);
    }
}
int main(void) {
    unsigned families = 0;
    for (const struct BCM5974_CONFIG *cfg = Bcm5974ConfigTable; cfg->ansi; cfg++) {
        if (cfg->tp_type >= TYPE2 && cfg->tp_type <= TYPE4) {
            testDevice(cfg);
            families |= 1u << cfg->tp_type;
        }
    }
    assert(families == ((1u << TYPE2) | (1u << TYPE3) | (1u << TYPE4)));
    puts("PASS: legacy USB Types 2-4 timestamps/wrap, zeroed reports, button-only input, truncated packets, and read error completion");
    return 0;
}
