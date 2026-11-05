// Battery.c: Magic Trackpad battery level for the Windows Bluetooth device list
//
// Windows reads battery levels natively only from Bluetooth LE devices and
// hands-free headsets. For this classic Bluetooth HID trackpad, the filter
// takes Apple's battery report from the input stream and publishes it.
//
// The filter must not ask for the report itself. A GET_REPORT sent to HidBth
// through the private transport route crashes Windows inside
// HidBthGetReportFlowCompletion (bugcheck 0xD1), because that path expects
// the request to come through HIDCLASS. Only reports the trackpad sends on its
// own are used.

#include <Driver.h>
#include "Battery.tmh"

// Settings > Bluetooth & devices shows this byte (0-100) for a device when
// any device node in its container carries it. The key is not in public
// headers; Windows itself writes it for LE devices and headsets.
// Defined directly: DEFINE_DEVPROPKEY only declares it here, because
// devpropdef.h is included before initguid.h.
static const DEVPROPKEY PtpFilterBatteryLevelKey = {
    { 0x104ea319, 0x6ee2, 0x4701, { 0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5 } }, 2 };

// Apple battery input report: [0x90][status][charge percent].
#define PTP_BATTERY_REPORT_ID      0x90
#define PTP_BATTERY_REPORT_SIZE    3

BOOLEAN
PtpFilterBatteryParse(
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ size_t Length,
    _Out_ PUCHAR Level
)
{
    *Level = 0;
    if (Buffer == NULL || Length < PTP_BATTERY_REPORT_SIZE || Buffer[0] != PTP_BATTERY_REPORT_ID) return FALSE;
    if (Buffer[2] > 100) return FALSE;
    *Level = Buffer[2];
    return TRUE;
}

VOID
PtpFilterBatteryRecord(
    _In_ PDEVICE_CONTEXT Context,
    _In_ UCHAR Level
)
{
    // Callable at DISPATCH_LEVEL. Publishing needs PASSIVE_LEVEL.
    InterlockedExchange(&Context->BatteryLevel, Level);
    if ((LONG)Level != Context->PublishedBatteryLevel) {
        WdfWorkItemEnqueue(Context->BatteryWorkItem);
    }
}

VOID
PtpFilterBatteryWorkItem(
    _In_ WDFWORKITEM WorkItem
)
{
    WDFDEVICE device = WdfWorkItemGetParentObject(WorkItem);
    PDEVICE_CONTEXT context = PtpFilterGetContext(device);
    WDF_DEVICE_PROPERTY_DATA propertyData;
    LONG level = context->BatteryLevel;
    UCHAR value;
    NTSTATUS status;

    if (level < 0 || level == context->PublishedBatteryLevel) return;
    value = (UCHAR)level;
    WDF_DEVICE_PROPERTY_DATA_INIT(&propertyData, &PtpFilterBatteryLevelKey);
    propertyData.Lcid = LOCALE_NEUTRAL;
    propertyData.Flags = PLUGPLAY_PROPERTY_PERSISTENT;
    status = WdfDeviceAssignProperty(device, &propertyData, DEVPROP_TYPE_BYTE, sizeof(value), &value);
    if (NT_SUCCESS(status)) context->PublishedBatteryLevel = level;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Battery %d%%, Status = %!STATUS!", level, status);
    TraceLoggingWrite(g_PtpBluetoothTrace, "BatteryLevel",
        TraceLoggingInt32(level, "Percent"), TraceLoggingNTStatus(status, "Status"));
}
