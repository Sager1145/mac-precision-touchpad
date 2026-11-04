#pragma once
#include <windows.h>
#include <string.h>

#define EXTERN_C_START
#define EXTERN_C_END
#define WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(type, name)
#define TraceEvents(...) ((void)0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_BUFFER_SIZE ((NTSTATUS)0xc0000206L)
#undef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000dL)
#define STATUS_NO_MORE_ENTRIES ((NTSTATUS)0x8000001aL)
#define NT_SUCCESS(status) ((NTSTATUS)(status) >= 0)
typedef LONG NTSTATUS; typedef void *WDFTIMER; typedef void EVT_WDF_TIMER(void);
typedef void *WDFUSBDEVICE, *WDFUSBPIPE, *WDFUSBINTERFACE, *WDFQUEUE;
typedef void *WDFDEVICE, *WDFDRIVER, *PWDFDEVICE_INIT, *WDFCONTEXT;
typedef void *HID_XFER_PACKET;
typedef ULONG USBD_STATUS;
typedef int WDF_POWER_DEVICE_STATE;
typedef struct { UCHAR unused; } USB_DEVICE_DESCRIPTOR;
typedef void EVT_WDF_DEVICE_PREPARE_HARDWARE(void);
typedef void EVT_WDF_DEVICE_D0_ENTRY(void);
typedef void EVT_WDF_DEVICE_D0_EXIT(void);
struct TEST_REQUEST;
typedef struct TEST_REQUEST *WDFREQUEST, *WDFMEMORY;

// Use the production packet definitions and device state, not copies.
#include <Hid.h>
#include <Device.h>

NTSTATUS WdfIoQueueRetrieveNextRequest(WDFQUEUE queue, WDFREQUEST *request);
NTSTATUS WdfRequestRetrieveOutputMemory(WDFREQUEST request, WDFMEMORY *memory);
NTSTATUS WdfMemoryCopyFromBuffer(WDFMEMORY memory, size_t offset, void *buffer, size_t length);
void WdfRequestCompleteWithInformation(WDFREQUEST request, NTSTATUS status, ULONG_PTR information);

// Rename only the clock API, so packet times are deterministic.
BOOL TestQueryPerformanceCounter(LARGE_INTEGER *counter);
#define QueryPerformanceCounter TestQueryPerformanceCounter
static inline INT AmtRawToInteger(USHORT x) { return (SHORT)x; }
