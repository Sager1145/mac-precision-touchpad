// BatteryService.c: Magic Trackpad battery level in Settings > Bluetooth &
// devices, over USB.
//
// The trackpad reports its charge in input report 0x90:
// [0x90][present/charging/discharging bits][percent]. Over USB it is on
// interface 0, which runs WinUSB (its HID descriptor is invalid for Windows),
// so this service asks for it with a HID class GET_REPORT and writes the level
// to that interface's device node. Writing a device property needs
// administrator rights, so this runs as a service.
//
// Bluetooth is not handled here. AmtPtpHidFilter takes the report the
// trackpad sends on its own from the input stream (Battery.c). Nothing may ask
// a Bluetooth trackpad for it: a GET_INPUT_REPORT that reaches HidBth, even
// through HIDCLASS from HidD_GetInputReport, can crash Windows in
// hidbth!HidBthGetReportFlowCompletion (it reads a missing MDL).

#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <initguid.h>
#include <devpkey.h>
#include <winusb.h>
#include <stdio.h>
#include <wchar.h>

#define SERVICE_NAME            L"AmtPtpBattery"
#define POLL_INTERVAL_MS        (5 * 60 * 1000)
#define ARRIVAL_DELAY_MS        3000
#define BATTERY_REPORT_ID       0x90
#define BATTERY_USAGE_PAGE      0xFF00
#define BATTERY_USAGE           0x14
#define APPLE_BLUETOOTH_VID     0x004C

// Settings reads this byte (0-100) from any device node in the device's
// container. Windows writes it for LE devices and headsets; it is not in
// public headers.
static const DEVPROPKEY BatteryLevelKey = {
    { 0x104ea319, 0x6ee2, 0x4701, { 0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5 } }, 2 };

// USB: interface 0 (the battery report's interface) runs WinUSB with this interface GUID
// (AmtPtpDevice.inf, AmtPtpBattery_UsbReg). Its HID report descriptor is invalid for
// Windows' HID parser, so the report is asked for with a HID class GET_REPORT instead.
DEFINE_GUID(GUID_AMTPTP_BATTERY_USB, 0x3d6e6b1c, 0x9a4f, 0x4c2e, 0x8b, 0x7d, 0x5e, 0x1f, 0x2a, 0x9c, 0x03, 0x24);

static SERVICE_STATUS_HANDLE g_statusHandle;
static SERVICE_STATUS g_status;
static HANDLE g_stopEvent;
static HANDLE g_wakeEvent;
static BOOL g_console;

static void
Log(_In_z_ _Printf_format_string_ const wchar_t* format, ...)
{
    wchar_t message[512];
    va_list args;
    va_start(args, format);
    _vsnwprintf_s(message, ARRAYSIZE(message), _TRUNCATE, format, args);
    va_end(args);
    if (g_console) wprintf(L"%s\n", message);
    else OutputDebugStringW(message);
}

static void
PublishBatteryLevel(_In_ DEVINST target, _In_ BYTE level)
{
    BYTE current = 0;
    DEVPROPTYPE type;
    ULONG size = sizeof(current);
    CONFIGRET result;

    if (CM_Get_DevNode_PropertyW(target, &BatteryLevelKey, &type, &current, &size, 0) == CR_SUCCESS &&
        type == DEVPROP_TYPE_BYTE && current == level) return;
    result = CM_Set_DevNode_PropertyW(target, &BatteryLevelKey, DEVPROP_TYPE_BYTE, &level, sizeof(level), 0);
    Log(L"Battery %u%%, publish result %lu", level, result);
}

// USB: GET_REPORT(Input, 0x90) on interface 0 through WinUSB. [0x90][status][charge 0-100]
static BOOL
ReadUsbBatteryLevel(_In_z_ const wchar_t* path, _Out_ BYTE* level)
{
    HANDLE device;
    WINUSB_INTERFACE_HANDLE usb;
    WINUSB_SETUP_PACKET setup;
    BYTE report[64];
    ULONG transferred = 0;
    BOOL ok = FALSE;

    *level = 0;
    device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL); // WinUSB requires overlapped handles
    if (device == INVALID_HANDLE_VALUE) return FALSE;
    if (WinUsb_Initialize(device, &usb)) {
        ZeroMemory(&setup, sizeof(setup));
        setup.RequestType = 0xA1;                        // device-to-host, class, interface
        setup.Request = 0x01;                            // HID GET_REPORT
        setup.Value = (0x01 << 8) | BATTERY_REPORT_ID;  // input report 0x90
        setup.Index = 0;                                 // interface 0
        setup.Length = sizeof(report);
        ZeroMemory(report, sizeof(report));
        if (WinUsb_ControlTransfer(usb, setup, report, sizeof(report), &transferred, NULL) &&
            transferred >= 3 && report[0] == BATTERY_REPORT_ID && report[2] >= 1 && report[2] <= 100) {
            *level = report[2];
            ok = TRUE;
        }
        WinUsb_Free(usb);
    }
    CloseHandle(device);
    return ok;
}

// Every trackpad on USB: reads the level and writes it to the battery interface's own device
// node, which is in the trackpad's USB device container (the "Magic Trackpad" entry).
static void
ScanUsbTrackpads(void)
{
    ULONG length = 0;
    wchar_t* list;
    const wchar_t* path;

    if (CM_Get_Device_Interface_List_SizeW(&length, (LPGUID)&GUID_AMTPTP_BATTERY_USB, NULL,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || length < 2) return;
    list = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length * sizeof(wchar_t));
    if (list == NULL) return;
    if (CM_Get_Device_Interface_ListW((LPGUID)&GUID_AMTPTP_BATTERY_USB, NULL, list, length,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS) {
        for (path = list; *path; path += wcslen(path) + 1) {
            wchar_t instanceId[MAX_DEVICE_ID_LEN];
            DEVPROPTYPE type;
            ULONG size = sizeof(instanceId);
            DEVINST target;
            BYTE level;
            if (!ReadUsbBatteryLevel(path, &level)) continue;
            Log(L"USB trackpad battery %u%%", level);
            if (CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_Device_InstanceId, &type,
                (PBYTE)instanceId, &size, 0) == CR_SUCCESS && type == DEVPROP_TYPE_STRING &&
                CM_Locate_DevNodeW(&target, instanceId, CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS) {
                PublishBatteryLevel(target, level);
            }
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
}

static DWORD CALLBACK
OnUsbInterfaceArrival(
    _In_ HCMNOTIFICATION notification,
    _In_opt_ PVOID context,
    _In_ CM_NOTIFY_ACTION action,
    _In_ PCM_NOTIFY_EVENT_DATA eventData,
    _In_ DWORD eventDataSize)
{
    UNREFERENCED_PARAMETER(notification);
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(eventData);
    UNREFERENCED_PARAMETER(eventDataSize);
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL) SetEvent(g_wakeEvent);
    return ERROR_SUCCESS;
}

static void
SetServiceState(_In_ DWORD state)
{
    if (g_console) return;
    g_status.dwCurrentState = state;
    g_status.dwControlsAccepted = state == SERVICE_RUNNING ? SERVICE_ACCEPT_STOP | SERVICE_ACCEPT_SHUTDOWN : 0;
    SetServiceStatus(g_statusHandle, &g_status);
}

static DWORD WINAPI
ServiceControl(_In_ DWORD control, _In_ DWORD eventType, _In_ LPVOID eventData, _In_ LPVOID context)
{
    UNREFERENCED_PARAMETER(eventType);
    UNREFERENCED_PARAMETER(eventData);
    UNREFERENCED_PARAMETER(context);
    switch (control) {
    case SERVICE_CONTROL_STOP:
    case SERVICE_CONTROL_SHUTDOWN:
        SetServiceState(SERVICE_STOP_PENDING);
        SetEvent(g_stopEvent);
        return NO_ERROR;
    case SERVICE_CONTROL_INTERROGATE:
        return NO_ERROR;
    default:
        return ERROR_CALL_NOT_IMPLEMENTED;
    }
}

static void
RunLoop(void)
{
    HCMNOTIFICATION usbNotification = NULL;
    CM_NOTIFY_FILTER filter;
    HANDLE events[2];

    ZeroMemory(&filter, sizeof(filter));
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    filter.u.DeviceInterface.ClassGuid = GUID_AMTPTP_BATTERY_USB;
    if (CM_Register_Notification(&filter, NULL, OnUsbInterfaceArrival, &usbNotification) != CR_SUCCESS) {
        usbNotification = NULL;
    }

    events[0] = g_stopEvent;
    events[1] = g_wakeEvent;
    ScanUsbTrackpads();
    for (;;) {
        DWORD wait = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, POLL_INTERVAL_MS);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            // Give the trackpad time to finish connecting before asking.
            if (WaitForSingleObject(g_stopEvent, ARRIVAL_DELAY_MS) == WAIT_OBJECT_0) break;
        }
        ScanUsbTrackpads();
    }

    if (usbNotification != NULL) CM_Unregister_Notification(usbNotification);
}

static void WINAPI
ServiceMain(_In_ DWORD argc, _In_ LPWSTR* argv)
{
    UNREFERENCED_PARAMETER(argc);
    UNREFERENCED_PARAMETER(argv);
    g_statusHandle = RegisterServiceCtrlHandlerExW(SERVICE_NAME, ServiceControl, NULL);
    if (g_statusHandle == NULL) return;
    g_status.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    SetServiceState(SERVICE_START_PENDING);
    g_stopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    g_wakeEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    if (g_stopEvent == NULL || g_wakeEvent == NULL) {
        g_status.dwWin32ExitCode = GetLastError();
        SetServiceState(SERVICE_STOPPED);
        return;
    }
    SetServiceState(SERVICE_RUNNING);
    RunLoop();
    SetServiceState(SERVICE_STOPPED);
}

int
wmain(_In_ int argc, _In_reads_(argc) wchar_t** argv)
{
    SERVICE_TABLE_ENTRYW table[] = { { (LPWSTR)SERVICE_NAME, ServiceMain }, { NULL, NULL } };

    // "--once" reads and publishes the level once, for testing from a console.
    if (argc > 1 && _wcsicmp(argv[1], L"--once") == 0) {
        g_console = TRUE;
        ScanUsbTrackpads();
        return 0;
    }
    return StartServiceCtrlDispatcherW(table) ? 0 : (int)GetLastError();
}
