// Input.c: Input handler routines

#include <Driver.h>
#include "Input.tmh"
#include "Type5Input.h"
#include "Type5Resampling.h"

static VOID
PtpFilterClearTransportRead(PDEVICE_CONTEXT Ctx, WDFREQUEST Request)
{
    KIRQL irql;
    KeAcquireSpinLock(&Ctx->InputLock, &irql);
    if (Ctx->TransportRequest == Request) Ctx->TransportRequest = NULL;
    KeReleaseSpinLock(&Ctx->InputLock, irql);
}

VOID
PtpFilterCancelTransportRead(_In_ PDEVICE_CONTEXT Ctx)
{
    KIRQL irql;
    WDFREQUEST request;
    KeAcquireSpinLock(&Ctx->InputLock, &irql);
    request = Ctx->TransportRequest;
    if (request != NULL) WdfObjectReference(request);
    KeReleaseSpinLock(&Ctx->InputLock, irql);
    if (request != NULL) {
        WdfRequestCancelSentRequest(request);
        WdfObjectDereference(request);
    }
}

VOID
PtpFilterInputProcessRequest(
	_In_ WDFDEVICE Device,
	_In_ WDFREQUEST Request
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;

	deviceContext = PtpFilterGetContext(Device);
	TraceLoggingWrite(g_PtpBluetoothTrace, "ConsumerRead",
		TraceLoggingBoolean(deviceContext->DeviceConfigured, "Configured"));
	status = WdfRequestForwardToIoQueue(Request, deviceContext->HidReadQueue);
	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfRequestForwardToIoQueue fails, status = %!STATUS!", status);
		WdfRequestComplete(Request, status);
		return;
	}

	// Only issue request when fully configured.
	// Otherwise we will let power recovery process to triage it
	if (deviceContext->DeviceConfigured == TRUE) {
		PtpFilterInputIssueTransportRequest(Device);
	}
}

VOID
PtpFilterWorkItemCallback(
	_In_ WDFWORKITEM WorkItem
)
{
	WDFDEVICE Device = WdfWorkItemGetParentObject(WorkItem);
	PtpFilterInputIssueTransportRequest(Device);
}

VOID
PtpFilterInputIssueTransportRequest(
	_In_ WDFDEVICE Device
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;

	WDF_OBJECT_ATTRIBUTES attributes;
	WDFREQUEST hidReadRequest;
	WDFMEMORY hidReadOutputMemory;
	PWORKER_REQUEST_CONTEXT requestContext;
	BOOLEAN requestStatus = FALSE;

	deviceContext = PtpFilterGetContext(Device);

	if (!deviceContext->DeviceConfigured ||
		InterlockedCompareExchange(&deviceContext->TransportReadActive, 1, 0) != 0) return;

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, WORKER_REQUEST_CONTEXT);
	attributes.ParentObject = Device;
	status = WdfRequestCreate(&attributes, deviceContext->HidIoTarget, &hidReadRequest);
	if (!NT_SUCCESS(status)) {
		// Retry startup when the Bluetooth transport becomes available.
		// Typically this should not fail for USB transport.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfRequestCreate fails, status = %!STATUS!", status);
		deviceContext->DeviceConfigured = FALSE;
		WdfTimerStart(deviceContext->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_MS(250));
		InterlockedExchange(&deviceContext->TransportReadActive, 0);
		return;
	}

	status = WdfMemoryCreateFromLookaside(deviceContext->HidReadBufferLookaside, &hidReadOutputMemory);
	if (!NT_SUCCESS(status)) {
		// tbh if you fail here, something seriously went wrong...request a restart.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfMemoryCreateFromLookaside fails, status = %!STATUS!", status);
		WdfObjectDelete(hidReadRequest);
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
		InterlockedExchange(&deviceContext->TransportReadActive, 0);
		return;
	}

	// Assign context information
	// And format HID read request.
	requestContext = WorkerRequestGetContext(hidReadRequest);
	requestContext->DeviceContext = deviceContext;
	requestContext->RequestMemory = hidReadOutputMemory;
	status = WdfIoTargetFormatRequestForInternalIoctl(deviceContext->HidIoTarget, hidReadRequest,
		IOCTL_HID_READ_REPORT, NULL, 0, hidReadOutputMemory, 0);
	if (!NT_SUCCESS(status)) {
		// tbh if you fail here, something seriously went wrong...request a restart.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfIoTargetFormatRequestForInternalIoctl fails, status = %!STATUS!", status);

		if (hidReadOutputMemory != NULL) {
			WdfObjectDelete(hidReadOutputMemory);
		}

		if (hidReadRequest != NULL) {
			WdfObjectDelete(hidReadRequest);
		}

		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
		InterlockedExchange(&deviceContext->TransportReadActive, 0);
		return;
	}

	// Set callback
	TraceLoggingWrite(g_PtpBluetoothTrace, "IssueTransportRead",
		TraceLoggingBoolean(deviceContext->DeviceConfigured, "Configured"));
	WdfRequestSetCompletionRoutine(hidReadRequest, PtpFilterInputRequestCompletionCallback, requestContext);
	KIRQL irql;
	KeAcquireSpinLock(&deviceContext->InputLock, &irql);
	deviceContext->TransportRequest = hidReadRequest;
	KeReleaseSpinLock(&deviceContext->InputLock, irql);

	requestStatus = WdfRequestSend(hidReadRequest, deviceContext->HidIoTarget, NULL);
	if (!requestStatus) {
		PtpFilterClearTransportRead(deviceContext, hidReadRequest);
		deviceContext->DeviceConfigured = FALSE;
		InterlockedExchange(&deviceContext->TransportReadActive, 0);
		// Retry promptly while a Bluetooth connection is being established.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! PtpFilterInputIssueTransportRequest request failed to sent");
		WdfTimerStart(deviceContext->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_MS(250));

		if (hidReadOutputMemory != NULL) {
			WdfObjectDelete(hidReadOutputMemory);
		}

		if (hidReadRequest != NULL) {
			WdfObjectDelete(hidReadRequest);
		}
	}
}

// Caller holds InputLock; complete the returned request after releasing it.
static WDFREQUEST
PtpFilterPrepareDelivery(PDEVICE_CONTEXT Ctx, LONGLONG Now, BOOLEAN Timer, NTSTATUS* Status)
{
    PTP_REPORT sample, report;
    WDFREQUEST request;
    WDFMEMORY memory;
    if (!Ctx->DeviceConfigured || !Ctx->Resampling.HasSample) return NULL;
    PtpFilterResampleBuild(&Ctx->Resampling, Now, Ctx->QpcFrequency,
        Ctx->X.max - Ctx->X.min, Ctx->Y.max - Ctx->Y.min, &sample);
    if (!Ctx->PtpReportTouch) sample.ContactCount = 0;
    if (!Ctx->PtpReportButton) sample.IsButtonClicked = FALSE;
    PtpFilterNormalizeReport(&sample, &Ctx->LastDeliveredReport, &report);
    if (!report.ContactCount && report.IsButtonClicked == Ctx->LastDeliveredReport.IsButtonClicked) return NULL;
    *Status = WdfIoQueueRetrieveNextRequest(Ctx->HidReadQueue, &request);
    if (!NT_SUCCESS(*Status)) return NULL;
    LONGLONG ticks = PtpFilterScanTicks(Now - Ctx->LastReportTime.QuadPart,
        Ctx->QpcFrequency, Ctx->LastScanTicks);
    report.ScanTime = (USHORT)ticks;
    *Status = WdfRequestRetrieveOutputMemory(request, &memory);
    if (NT_SUCCESS(*Status)) *Status = WdfMemoryCopyFromBuffer(memory, 0, &report, sizeof(report));
    if (NT_SUCCESS(*Status)) {
        Ctx->LastDeliveredReport = report;
        Ctx->LastScanTicks = ticks;
        Ctx->LastEmitTime = Now;
        WdfRequestSetInformation(request, sizeof(report));
        TraceLoggingWrite(g_PtpBluetoothTrace, "DeliveredInput",
            TraceLoggingBoolean(Timer, "Timer"), TraceLoggingInt64(Now, "Qpc"),
            TraceLoggingBinary(&report, sizeof(report), "Report"));
    }
    return request;
}

VOID
PtpFilterResamplingTimerCallback(_In_ WDFTIMER Timer)
{
    PDEVICE_CONTEXT ctx = PtpFilterGetContext(WdfTimerGetParentObject(Timer));
    WDFREQUEST request = NULL;
    NTSTATUS status = STATUS_SUCCESS;
    KIRQL oldIrql;
    KeAcquireSpinLock(&ctx->InputLock, &oldIrql);
    LARGE_INTEGER now = KeQueryPerformanceCounter(NULL);
    if (ctx->QpcFrequency > 0 &&
        (now.QuadPart - ctx->LastEmitTime) * 1000 / ctx->QpcFrequency >= PTP_RESAMPLE_PERIOD_MS - 1)
        request = PtpFilterPrepareDelivery(ctx, now.QuadPart, TRUE, &status);
    KeReleaseSpinLock(&ctx->InputLock, oldIrql);
    if (request != NULL) WdfRequestComplete(request, status);
}

VOID
PtpFilterInputRequestCompletionCallback(
    _In_ WDFREQUEST Request,
    _In_ WDFIOTARGET Target,
    _In_ PWDF_REQUEST_COMPLETION_PARAMS Params,
    _In_ WDFCONTEXT Context
)
{
    PWORKER_REQUEST_CONTEXT requestContext = (PWORKER_REQUEST_CONTEXT)Context;
    PDEVICE_CONTEXT ctx = requestContext->DeviceContext;
    WDFMEMORY rawMemory = requestContext->RequestMemory;
    WDFREQUEST ptpRequest = NULL;
    PTP_REPORT physical;
    NTSTATUS status = Params->IoStatus.Status;
    NTSTATUS outputStatus = STATUS_SUCCESS;
    size_t capacity = 0, length;
    PUCHAR buffer;
    KIRQL oldIrql;
    BOOLEAN continuePump = TRUE;
    UNREFERENCED_PARAMETER(Target);
    TraceLoggingWrite(g_PtpBluetoothTrace, "TransportReadComplete",
        TraceLoggingNTStatus(status, "Status"),
        TraceLoggingUInt64(WdfRequestGetInformation(Request), "Length"),
        TraceLoggingBoolean(ctx->DeviceConfigured, "Configured"));
    if (!NT_SUCCESS(status)) {
        continuePump = FALSE;
        // Power exit clears DeviceConfigured before cancelling reads. Errors
        // while still configured, including a transient link loss, need retry.
        if (ctx->DeviceConfigured) {
            LONG failures = InterlockedIncrement(&ctx->ReadErrorCount);
            if (failures >= 4) ctx->DeviceConfigured = FALSE;
            TraceLoggingWrite(g_PtpBluetoothTrace, "ReadRecovery", TraceLoggingInt32(failures, "Failures"));
            WdfTimerStart(ctx->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_MS(failures < 4 ? 20 : 500));
        }
        goto cleanup;
    }
    if (!ctx->DeviceConfigured) goto cleanup;
    InterlockedExchange(&ctx->ReadErrorCount, 0);
    length = (size_t)WdfRequestGetInformation(Request);
    buffer = WdfMemoryGetBuffer(rawMemory, &capacity);
    if (length == 0 || length > capacity || buffer == NULL) goto cleanup;
    TraceLoggingWrite(g_PtpBluetoothTrace, "PhysicalPacket", TraceLoggingBinary(buffer, (ULONG)length, "Packet"));
    if (buffer[0] != (ctx->VendorID == HID_VID_APPLE_BT ? 0x31 : 0x02)) {
        // If firmware has returned to mouse mode, restore multitouch mode.
        if (ctx->VendorID == HID_VID_APPLE_BT && buffer[0] == 0x02) {
            ctx->DeviceConfigured = FALSE;
            continuePump = FALSE;
            WdfTimerStart(ctx->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_MS(100));
        }
        goto cleanup;
    }
    KeAcquireSpinLock(&ctx->InputLock, &oldIrql);
    LARGE_INTEGER now = KeQueryPerformanceCounter(NULL);
    if (!PtpFilterBuildType5Report(buffer, length, ctx->InputHeaderSize,
        ctx->X.min, ctx->X.max, ctx->Y.min, ctx->Y.max, 0,
        ctx->PtpReportTouch, ctx->PtpReportButton, &ctx->LastPhysicalReport, &physical)) {
        TraceLoggingWrite(g_PtpBluetoothTrace, "MalformedPacket", TraceLoggingUInt32((ULONG)length, "Length"));
        KeReleaseSpinLock(&ctx->InputLock, oldIrql);
        goto cleanup;
    }
    LONGLONG sourceTime = PtpFilterSourceTime(&ctx->Resampling,
        buffer + ctx->InputHeaderSize - 4, now.QuadPart, ctx->QpcFrequency);
    BOOLEAN transition = PtpFilterResampleOnSample(&ctx->Resampling, &physical,
        sourceTime, now.QuadPart, ctx->QpcFrequency);
    ctx->LastPhysicalReport = physical;
    if (transition) ptpRequest = PtpFilterPrepareDelivery(ctx, now.QuadPart, FALSE, &outputStatus);
    KeReleaseSpinLock(&ctx->InputLock, oldIrql);
    if (ptpRequest != NULL) WdfRequestComplete(ptpRequest, outputStatus);

cleanup:
    PtpFilterClearTransportRead(ctx, Request);
    WdfObjectDelete(Request);
    if (rawMemory != NULL) WdfObjectDelete(rawMemory);
    InterlockedExchange(&ctx->TransportReadActive, 0);
    if (continuePump && ctx->DeviceConfigured) WdfWorkItemEnqueue(ctx->HidTransportRecoveryWorkItem);
}
