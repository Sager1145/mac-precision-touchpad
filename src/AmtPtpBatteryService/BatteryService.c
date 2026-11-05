// BatteryService.c: Magic Trackpad battery level for the Windows Bluetooth
// device list.
//
// Windows reads battery levels natively only from Bluetooth LE devices and
// hands-free headsets. A Magic Trackpad on classic Bluetooth reports its
// charge in input report 0x90 of its vendor collection (usage page 0xFF00,
// usage 0x14): [0x90][present/charging/discharging bits][percent].
//
// The report is requested through the standard HID API, which goes through
// HIDCLASS to HidBth as Windows expects. The kernel filter must not request it
// itself: a GET_REPORT it sends straight to HidBth crashes Windows.
//
// The level is written to the trackpad's touch collection, where Settings >
// Bluetooth & devices shows it. Writing a device property needs
// administrator rights, so this runs as a service.

#include <windows.h>
#include <cfgmgr32.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <initguid.h>
#include <devpkey.h>
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

static BOOL
ContainsInsensitive(_In_z_ const wchar_t* text, _In_z_ const wchar_t* needle)
{
    size_t length = wcslen(needle);
    for (; *text; text++) {
        if (_wcsnicmp(text, needle, length) == 0) return TRUE;
    }
    return FALSE;
}

static BOOL
IsTrackpadPath(_In_z_ const wchar_t* path)
{
    // Bluetooth HID service UUID with Apple's Bluetooth vendor ID, then the
    // Magic Trackpad 2 or Magic Trackpad USB-C product ID.
    return ContainsInsensitive(path, L"{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004C_PID&0265") ||
        ContainsInsensitive(path, L"{00001124-0000-1000-8000-00805f9b34fb}_VID&0001004C_PID&0324");
}

static BOOL
ReadBatteryLevel(_In_z_ const wchar_t* path, _Out_ BYTE* level)
{
    HANDLE device;
    HIDD_ATTRIBUTES attributes;
    PHIDP_PREPARSED_DATA preparsed;
    HIDP_CAPS caps;
    BYTE report[64];
    BOOL ok = FALSE;

    *level = 0;
    device = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING, 0, NULL);
    if (device == INVALID_HANDLE_VALUE) return FALSE;

    attributes.Size = sizeof(attributes);
    if (HidD_GetAttributes(device, &attributes) && attributes.VendorID == APPLE_BLUETOOTH_VID &&
        HidD_GetPreparsedData(device, &preparsed)) {
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS &&
            caps.UsagePage == BATTERY_USAGE_PAGE && caps.Usage == BATTERY_USAGE &&
            caps.InputReportByteLength >= 3 && caps.InputReportByteLength <= sizeof(report)) {
            ZeroMemory(report, sizeof(report));
            report[0] = BATTERY_REPORT_ID;
            if (HidD_GetInputReport(device, report, caps.InputReportByteLength) &&
                report[0] == BATTERY_REPORT_ID && report[2] <= 100) {
                *level = report[2];
                ok = TRUE;
            }
        }
        HidD_FreePreparsedData(preparsed);
    }
    CloseHandle(device);
    return ok;
}

static BOOL
FindTouchCollection(_In_z_ const wchar_t* path, _Out_ DEVINST* target)
{
    wchar_t instanceId[MAX_DEVICE_ID_LEN];
    wchar_t childId[MAX_DEVICE_ID_LEN];
    DEVPROPTYPE type;
    ULONG size = sizeof(instanceId);
    DEVINST vendorCollection, parent, child;

    if (CM_Get_Device_Interface_PropertyW(path, &DEVPKEY_Device_InstanceId, &type,
        (PBYTE)instanceId, &size, 0) != CR_SUCCESS || type != DEVPROP_TYPE_STRING) return FALSE;
    if (CM_Locate_DevNodeW(&vendorCollection, instanceId, CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) return FALSE;

    // Settings showed the level from the touch collection (Col01), a sibling
    // of this vendor collection. Fall back to this node if it is missing.
    *target = vendorCollection;
    if (CM_Get_Parent(&parent, vendorCollection, 0) != CR_SUCCESS) return TRUE;
    if (CM_Get_Child(&child, parent, 0) != CR_SUCCESS) return TRUE;
    do {
        if (CM_Get_Device_IDW(child, childId, ARRAYSIZE(childId), 0) == CR_SUCCESS &&
            ContainsInsensitive(childId, L"&COL01\\")) {
            *target = child;
            break;
        }
    } while (CM_Get_Sibling(&child, child, 0) == CR_SUCCESS);
    return TRUE;
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

static void
ScanTrackpads(void)
{
    GUID hidGuid;
    ULONG length = 0;
    wchar_t* list;
    const wchar_t* path;

    HidD_GetHidGuid(&hidGuid);
    if (CM_Get_Device_Interface_List_SizeW(&length, &hidGuid, NULL,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT) != CR_SUCCESS || length < 2) return;
    list = (wchar_t*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, length * sizeof(wchar_t));
    if (list == NULL) return;
    if (CM_Get_Device_Interface_ListW(&hidGuid, NULL, list, length,
        CM_GET_DEVICE_INTERFACE_LIST_PRESENT) == CR_SUCCESS) {
        for (path = list; *path; path += wcslen(path) + 1) {
            BYTE level;
            DEVINST target;
            // Only open the trackpad's own collections, never other HID devices.
            if (!IsTrackpadPath(path)) continue;
            if (!ReadBatteryLevel(path, &level)) continue;
            Log(L"Trackpad battery %u%%", level);
            if (FindTouchCollection(path, &target)) PublishBatteryLevel(target, level);
        }
    }
    HeapFree(GetProcessHeap(), 0, list);
}

static DWORD CALLBACK
OnHidInterfaceArrival(
    _In_ HCMNOTIFICATION notification,
    _In_opt_ PVOID context,
    _In_ CM_NOTIFY_ACTION action,
    _In_ PCM_NOTIFY_EVENT_DATA eventData,
    _In_ DWORD eventDataSize)
{
    UNREFERENCED_PARAMETER(notification);
    UNREFERENCED_PARAMETER(context);
    UNREFERENCED_PARAMETER(eventDataSize);
    if (action == CM_NOTIFY_ACTION_DEVICEINTERFACEARRIVAL &&
        IsTrackpadPath(eventData->u.DeviceInterface.SymbolicLink)) {
        SetEvent(g_wakeEvent);
    }
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
    HCMNOTIFICATION notification = NULL;
    CM_NOTIFY_FILTER filter;
    HANDLE events[2];

    ZeroMemory(&filter, sizeof(filter));
    filter.cbSize = sizeof(filter);
    filter.FilterType = CM_NOTIFY_FILTER_TYPE_DEVICEINTERFACE;
    HidD_GetHidGuid(&filter.u.DeviceInterface.ClassGuid);
    if (CM_Register_Notification(&filter, NULL, OnHidInterfaceArrival, &notification) != CR_SUCCESS) {
        notification = NULL;
    }

    events[0] = g_stopEvent;
    events[1] = g_wakeEvent;
    ScanTrackpads();
    for (;;) {
        DWORD wait = WaitForMultipleObjects(ARRAYSIZE(events), events, FALSE, POLL_INTERVAL_MS);
        if (wait == WAIT_OBJECT_0) break;
        if (wait == WAIT_OBJECT_0 + 1) {
            // Give the trackpad time to finish connecting before asking.
            if (WaitForSingleObject(g_stopEvent, ARRIVAL_DELAY_MS) == WAIT_OBJECT_0) break;
        }
        ScanTrackpads();
    }

    if (notification != NULL) CM_Unregister_Notification(notification);
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
        ScanTrackpads();
        return 0;
    }
    return StartServiceCtrlDispatcherW(table) ? 0 : (int)GetLastError();
}
