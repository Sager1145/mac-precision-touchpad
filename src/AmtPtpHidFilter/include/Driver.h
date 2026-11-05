// Driver.h: common definition
#pragma once

#include <ntddk.h>
#include <wdf.h>
#include <usb.h>
#include <usbdlib.h>
#include <wdfusb.h>
#include <initguid.h>
#include <hidport.h>
#include <TraceLoggingProvider.h>

TRACELOGGING_DECLARE_PROVIDER(g_PtpBluetoothTrace);

// Raw touch packets and delivered reports describe what the user is doing.
// Log them only at verbose level under their own keyword, so a session must
// ask for them explicitly.
#define PTP_TRACE_LEVEL_VERBOSE   5
#define PTP_TRACE_KEYWORD_INPUT   0x1

#include "Trace.h"

EXTERN_C_START

// Common entry points
DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD PtpFilterEvtDeviceAdd;
EVT_WDF_OBJECT_CONTEXT_CLEANUP PtpFilterEvtDriverContextCleanup;

// Device specific routines
#include "HidMiniport.h"
#include "Device.h"
#include "Queue.h"
#include "Hac.h"
#include "Diagnostics.h"
#include "Metadata/StaticHidRegistry.h"
#include "HidDevice.h"
#include "Input.h"

// Pool Tag
#define PTP_LIST_POOL_TAG 'LTPA'
#define REPORT_BUFFER_SIZE   1024

EXTERN_C_END
