#define _WIN32_WINNT 0x0A00
#include <driver.h>
#undef QueryPerformanceCounter
#include <assert.h>
#include <stdio.h>
#define TraceLoggingWrite(...) ((void)0)
#define WDF_REL_TIMEOUT_IN_MS(ms) (-(LONGLONG)(ms) * 10000)
static BOOLEAN failTimer;
static unsigned fallbackStarts;
static volatile LONG ticks;
static LONGLONG tickTimes[256];
static HANDLE TestCreateTimer(LPSECURITY_ATTRIBUTES a, LPCWSTR name, DWORD flags, DWORD access)
{
    if (failTimer) { SetLastError(ERROR_INVALID_PARAMETER); return NULL; }
    return CreateWaitableTimerExW(a, name, flags, access);
}
#define CreateWaitableTimerExW TestCreateTimer
static BOOL WdfTimerStop(WDFTIMER timer, BOOLEAN wait)
{
    (void)timer; (void)wait; return TRUE;
}
static BOOL WdfTimerStart(WDFTIMER timer, LONGLONG due)
{
    (void)timer; assert(due < 0); fallbackStarts++; return TRUE;
}
void AmtPtpInterpolationTick(PDEVICE_CONTEXT ctx)
{
    LARGE_INTEGER now;
    (void)ctx;
    QueryPerformanceCounter(&now);
    LONG index = InterlockedIncrement(&ticks) - 1;
    if (index < 256) tickTimes[index] = now.QuadPart;
}
#include "native-clock-under-test.inc"
static void assertClosed(DEVICE_CONTEXT *ctx)
{
    assert(!ctx->ResamplingThread && !ctx->ResamplingWaitTimer && !ctx->ResamplingStopEvent);
}
int main(void)
{
    DEVICE_CONTEXT ctx = {0};
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    for (unsigned cycle = 0; cycle < 3; cycle++) {
        AmtPtpStartResampling(&ctx);
        assert(ctx.ResamplingThread && ctx.ResamplingWaitTimer && ctx.ResamplingStopEvent);
        Sleep(120);
        AmtPtpStopResampling(&ctx);
        assertClosed(&ctx);
        LONG stopped = ticks;
        Sleep(20);
        assert(ticks == stopped); // Worker is joined before context/handles can be freed.
    }
    assert(ticks >= 3 && fallbackStarts == 0);
    double total = 0;
    unsigned intervals = 0;
    for (LONG i = 1; i < ticks; i++) {
        double ms = (double)(tickTimes[i] - tickTimes[i-1]) * 1000.0 / frequency.QuadPart;
        if (ms < 20) { total += ms; intervals++; }
    }
    assert(intervals > 0);
    failTimer = TRUE;
    AmtPtpStartResampling(&ctx);
    assertClosed(&ctx);
    assert(fallbackStarts == 1);
    AmtPtpStopResampling(&ctx); // Partial startup and repeated stop are safe.
    assertClosed(&ctx);
    printf("PASS: native timer start/stop/restart, joined worker, handle cleanup, fallback; observed mean interval %.3f ms\n", total/intervals);
    return 0;
}
