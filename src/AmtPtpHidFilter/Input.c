// Input.c: Input handler routines

#include <Driver.h>
#include "Input.tmh"
#include "Type5Input.h"
#include "Type5Resampling.h"

VOID
PtpFilterInputProcessRequest(
	_In_ WDFDEVICE Device,
	_In_ WDFREQUEST Request
)
{
	NTSTATUS status;
	PDEVICE_CONTEXT deviceContext;
	WDF_OBJECT_ATTRIBUTES attributes;

	deviceContext = PtpFilterGetContext(Device);
	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, HID_DELIVERY_CONTEXT);
	status = WdfObjectAllocateContext(Request, &attributes, NULL);
	if (!NT_SUCCESS(status) && status != STATUS_OBJECT_NAME_EXISTS) {
		WdfRequestComplete(Request, status);
		return;
	}
	TraceLoggingWrite(g_PtpBluetoothTrace, "ConsumerRead",
		TraceLoggingBoolean(deviceContext->DeviceConfigured, "Configured"));
	status = WdfRequestForwardToIoQueue(Request, deviceContext->HidReadQueue);
	if (!NT_SUCCESS(status)) {
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_INPUT, "%!FUNC! WdfRequestForwardToIoQueue fails, status = %!STATUS!", status);
		WdfRequestComplete(Request, status);
		return;
	}

}

static VOID
PtpFilterFinishTransportRead(PDEVICE_CONTEXT Ctx, BOOLEAN Continue, ULONG RetryMs)
{
    KIRQL irql;
    KeAcquireSpinLock(&Ctx->InputLock, &irql);
    Ctx->TransportReadActive = 0;
    if (!Ctx->Stopping) {
        if (RetryMs != 0)
            WdfTimerStart(Ctx->HidTransportRecoveryTimer, WDF_REL_TIMEOUT_IN_MS(RetryMs));
        else if (Continue && Ctx->DeviceConfigured)
            WdfWorkItemEnqueue(Ctx->HidTransportRecoveryWorkItem);
    }
    KeSetEvent(&Ctx->TransportIdle, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Ctx->InputLock, irql);
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
	KIRQL irql;

	deviceContext = PtpFilterGetContext(Device);

	KeAcquireSpinLock(&deviceContext->InputLock, &irql);
	if (deviceContext->Stopping || !deviceContext->DeviceConfigured || deviceContext->TransportReadActive) {
		KeReleaseSpinLock(&deviceContext->InputLock, irql);
		return;
	}
	deviceContext->TransportReadActive = 1;
	KeClearEvent(&deviceContext->TransportIdle);
	KeReleaseSpinLock(&deviceContext->InputLock, irql);

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, WORKER_REQUEST_CONTEXT);
	attributes.ParentObject = Device;
	status = WdfRequestCreate(&attributes, deviceContext->HidIoTarget, &hidReadRequest);
	if (!NT_SUCCESS(status)) {
		// Retry startup when the Bluetooth transport becomes available.
		// Typically this should not fail for USB transport.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfRequestCreate fails, status = %!STATUS!", status);
		deviceContext->DeviceConfigured = FALSE;
		PtpFilterFinishTransportRead(deviceContext, FALSE, 250);
		return;
	}

	status = WdfMemoryCreateFromLookaside(deviceContext->HidReadBufferLookaside, &hidReadOutputMemory);
	if (!NT_SUCCESS(status)) {
		// tbh if you fail here, something seriously went wrong...request a restart.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WdfMemoryCreateFromLookaside fails, status = %!STATUS!", status);
		WdfObjectDelete(hidReadRequest);
		WdfDeviceSetFailed(deviceContext->Device, WdfDeviceFailedAttemptRestart);
		PtpFilterFinishTransportRead(deviceContext, FALSE, 0);
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
		PtpFilterFinishTransportRead(deviceContext, FALSE, 0);
		return;
	}

	// Set callback
	TraceLoggingWrite(g_PtpBluetoothTrace, "IssueTransportRead",
		TraceLoggingBoolean(deviceContext->DeviceConfigured, "Configured"));
	WdfRequestSetCompletionRoutine(hidReadRequest, PtpFilterInputRequestCompletionCallback, requestContext);

	requestStatus = WdfRequestSend(hidReadRequest, deviceContext->HidIoTarget, NULL);
	if (!requestStatus) {
		deviceContext->DeviceConfigured = FALSE;
		// Retry promptly while a Bluetooth connection is being established.
		TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! PtpFilterInputIssueTransportRequest request failed to sent");

		if (hidReadOutputMemory != NULL) {
			WdfObjectDelete(hidReadOutputMemory);
		}

		if (hidReadRequest != NULL) {
			WdfObjectDelete(hidReadRequest);
		}
		PtpFilterFinishTransportRead(deviceContext, FALSE, 250);
	}
}

// Caller holds InputLock. Exactly one drainer completes the prepared FIFO.
static BOOLEAN
PtpFilterPrepareDelivery(PDEVICE_CONTEXT Ctx, LONGLONG Now, BOOLEAN Timer)
{
    PTP_REPORT sample, report;
    WDFREQUEST request;
    WDFMEMORY memory;
    NTSTATUS status;
    if (Ctx->Stopping || !Ctx->Resampling.HasSample) return FALSE;
    if (!Ctx->DeviceConfigured && Now - Ctx->Resampling.ArrivalTime <
        Ctx->QpcFrequency * PTP_RESAMPLE_STALE_MS / 1000) return FALSE;
    PtpFilterResampleBuild(&Ctx->Resampling, Now, Ctx->QpcFrequency,
        Ctx->X.max - Ctx->X.min, Ctx->Y.max - Ctx->Y.min, &sample);
    if (!Ctx->PtpReportTouch) sample.ContactCount = 0;
    if (!Ctx->PtpReportButton) sample.IsButtonClicked = FALSE;
    PtpFilterNormalizeReport(&sample, &Ctx->LastDeliveredReport, &report);
    if (!report.ContactCount && report.IsButtonClicked == Ctx->LastDeliveredReport.IsButtonClicked) return FALSE;
    status = WdfIoQueueRetrieveNextRequest(Ctx->HidReadQueue, &request);
    if (!NT_SUCCESS(status)) return FALSE;
    LONGLONG ticks = PtpFilterScanTicks(Now - Ctx->LastReportTime.QuadPart,
        Ctx->QpcFrequency, Ctx->LastScanTicks);
    report.ScanTime = (USHORT)ticks;
    status = WdfRequestRetrieveOutputMemory(request, &memory);
    if (NT_SUCCESS(status)) status = WdfMemoryCopyFromBuffer(memory, 0, &report, sizeof(report));
    if (NT_SUCCESS(status)) {
        Ctx->LastDeliveredReport = report;
        Ctx->LastScanTicks = ticks;
        Ctx->LastEmitTime = Now;
        WdfRequestSetInformation(request, sizeof(report));
        TraceLoggingWrite(g_PtpBluetoothTrace, "DeliveredInput",
            TraceLoggingLevel(PTP_TRACE_LEVEL_VERBOSE), TraceLoggingKeyword(PTP_TRACE_KEYWORD_INPUT),
            TraceLoggingBoolean(Timer, "Timer"), TraceLoggingInt64(Now, "Qpc"),
            TraceLoggingBinary(&report, sizeof(report), "Report"));
    }
    PHID_DELIVERY_CONTEXT delivery = HidDeliveryGetContext(request);
    delivery->Request = request;
    delivery->Status = status;
    InsertTailList(&Ctx->DeliveryQueue, &delivery->Entry);
    if (Ctx->DeliveryActive) return FALSE;
    Ctx->DeliveryActive = TRUE;
    return TRUE;
}

static VOID
PtpFilterDrainDelivery(PDEVICE_CONTEXT Ctx)
{
    KIRQL irql;
    for (;;) {
        KeAcquireSpinLock(&Ctx->InputLock, &irql);
        if (IsListEmpty(&Ctx->DeliveryQueue)) {
            Ctx->DeliveryActive = FALSE;
            KeReleaseSpinLock(&Ctx->InputLock, irql);
            return;
        }
        PHID_DELIVERY_CONTEXT delivery = CONTAINING_RECORD(
            RemoveHeadList(&Ctx->DeliveryQueue), HID_DELIVERY_CONTEXT, Entry);
        WDFREQUEST request = delivery->Request;
        NTSTATUS status = Ctx->Stopping ? STATUS_CANCELLED : delivery->Status;
        KeReleaseSpinLock(&Ctx->InputLock, irql);
        WdfRequestComplete(request, status);
    }
}

VOID
PtpFilterResamplingTimerCallback(_In_ WDFTIMER Timer)
{
    PDEVICE_CONTEXT ctx = PtpFilterGetContext(WdfTimerGetParentObject(Timer));
    BOOLEAN drain = FALSE;
    KIRQL oldIrql;
    KeAcquireSpinLock(&ctx->InputLock, &oldIrql);
    LARGE_INTEGER now = KeQueryPerformanceCounter(NULL);
    if (ctx->QpcFrequency > 0 &&
        now.QuadPart - ctx->LastEmitTime >= ctx->QpcFrequency / 1000 * (PTP_RESAMPLE_PERIOD_MS - 1))
        drain = PtpFilterPrepareDelivery(ctx, now.QuadPart, TRUE);
    KeReleaseSpinLock(&ctx->InputLock, oldIrql);
    if (drain) PtpFilterDrainDelivery(ctx);
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
    BOOLEAN drain = FALSE;
    PTP_REPORT physical;
    NTSTATUS status = Params->IoStatus.Status;
    size_t capacity = 0, length;
    PUCHAR buffer;
    KIRQL oldIrql;
    BOOLEAN continuePump = TRUE;
    ULONG retryMs = 0;
    UNREFERENCED_PARAMETER(Target);
    TraceLoggingWrite(g_PtpBluetoothTrace, "TransportReadComplete",
        TraceLoggingNTStatus(status, "Status"),
        TraceLoggingUInt64(WdfRequestGetInformation(Request), "Length"),
        TraceLoggingBoolean(ctx->DeviceConfigured, "Configured"));
    if (!NT_SUCCESS(status)) {
        continuePump = FALSE;
        // Power exit clears DeviceConfigured before cancelling reads. Errors
        // while still configured, including a transient link loss, need retry.
        KeAcquireSpinLock(&ctx->InputLock, &oldIrql);
        if (!ctx->Stopping && ctx->DeviceConfigured) {
            LONG failures = InterlockedIncrement(&ctx->ReadErrorCount);
            if (failures >= 4) ctx->DeviceConfigured = FALSE;
            TraceLoggingWrite(g_PtpBluetoothTrace, "ReadRecovery", TraceLoggingInt32(failures, "Failures"));
            retryMs = failures < 4 ? 20 : 500;
        }
        KeReleaseSpinLock(&ctx->InputLock, oldIrql);
        goto cleanup;
    }
    if (!ctx->DeviceConfigured) goto cleanup;
    InterlockedExchange(&ctx->ReadErrorCount, 0);
    length = (size_t)WdfRequestGetInformation(Request);
    buffer = WdfMemoryGetBuffer(rawMemory, &capacity);
    if (length == 0 || length > capacity || buffer == NULL) goto cleanup;
    TraceLoggingWrite(g_PtpBluetoothTrace, "PhysicalPacket",
        TraceLoggingLevel(PTP_TRACE_LEVEL_VERBOSE), TraceLoggingKeyword(PTP_TRACE_KEYWORD_INPUT),
        TraceLoggingBinary(buffer, (ULONG)length, "Packet"));
    if (buffer[0] != (ctx->VendorID == HID_VID_APPLE_BT ? 0x31 : 0x02)) {
        // If firmware has returned to mouse mode, restore multitouch mode.
        if (ctx->VendorID == HID_VID_APPLE_BT && buffer[0] == 0x02) {
            KeAcquireSpinLock(&ctx->InputLock, &oldIrql);
            ctx->DeviceConfigured = FALSE;
            continuePump = FALSE;
            retryMs = 100;
            KeReleaseSpinLock(&ctx->InputLock, oldIrql);
        }
        goto cleanup;
    }
    KeAcquireSpinLock(&ctx->InputLock, &oldIrql);
    if (ctx->Stopping || !ctx->DeviceConfigured) {
        KeReleaseSpinLock(&ctx->InputLock, oldIrql);
        goto cleanup;
    }
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
    if (transition) drain = PtpFilterPrepareDelivery(ctx, now.QuadPart, FALSE);
    KeReleaseSpinLock(&ctx->InputLock, oldIrql);
    if (drain) PtpFilterDrainDelivery(ctx);

cleanup:
    WdfObjectDelete(Request);
    if (rawMemory != NULL) WdfObjectDelete(rawMemory);
    PtpFilterFinishTransportRead(ctx, continuePump, retryMs);
}
