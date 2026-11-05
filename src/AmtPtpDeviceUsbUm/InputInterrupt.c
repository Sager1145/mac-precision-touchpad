// InputInterrupt.c: Handles device input event

#include <driver.h>
#include "InputInterrupt.tmh"
#include "../AmtPtpHidFilter/include/Type5Input.h"

//
// Forward declarations for the resampling helpers used by the Type 5 handler.
//
static VOID
AmtPtpSetScanTime(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PPTP_REPORT Report,
	_In_ LONGLONG NowQpc
);

static NTSTATUS
AmtPtpEmitReport(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PPTP_REPORT Report,
	_In_ LONGLONG NowQpc
);

static BOOLEAN
AmtPtpInterpOnSample(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PPTP_REPORT Sample,
	_In_ LARGE_INTEGER Now
);

static VOID
AmtPtpInterpBuildReport(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ LONGLONG NowQpc,
	_Out_ PPTP_REPORT Report
);

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AmtPtpConfigContReaderForInterruptEndPoint(
	_In_ PDEVICE_CONTEXT DeviceContext
)
{

	WDF_USB_CONTINUOUS_READER_CONFIG contReaderConfig;
	NTSTATUS status;
	size_t transferLength = 0;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	switch (DeviceContext->DeviceInfo->tp_type)
	{
		case TYPE1:
			transferLength = HEADER_TYPE1 + FSIZE_TYPE1 * MAX_FINGERS;
			break;
		case TYPE2:
			transferLength = HEADER_TYPE2 + FSIZE_TYPE2 * MAX_FINGERS;
			break;
		case TYPE3:
			transferLength = HEADER_TYPE3 + FSIZE_TYPE3 * MAX_FINGERS;
			break;
		case TYPE4:
			transferLength = HEADER_TYPE4 + FSIZE_TYPE4 * MAX_FINGERS;
			break;
		case TYPE5:
			transferLength = HEADER_TYPE5 + FSIZE_TYPE5 * MAX_FINGERS;
			break;
	}

	if (transferLength <= 0) {
		status = STATUS_UNKNOWN_REVISION;
		return status;
	}

	WDF_USB_CONTINUOUS_READER_CONFIG_INIT(
		&contReaderConfig,
		AmtPtpEvtUsbInterruptPipeReadComplete,
		DeviceContext,		// Context
		transferLength		// Calculate transferred length by device information
	);

	contReaderConfig.EvtUsbTargetPipeReadersFailed = AmtPtpEvtUsbInterruptReadersFailed;

	// Remember to turn it on in D0 entry
	status = WdfUsbTargetPipeConfigContinuousReader(
		DeviceContext->InterruptPipe,
		&contReaderConfig
	);

	if (!NT_SUCCESS(status)) {
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"%!FUNC! AmtPtpConfigContReaderForInterruptEndPoint failed with Status code %!STATUS!",
			status
		);
		return status;
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);

	return STATUS_SUCCESS;

}

_IRQL_requires_(PASSIVE_LEVEL)
VOID
AmtPtpEvtUsbInterruptPipeReadComplete(
	_In_ WDFUSBPIPE  Pipe,
	_In_ WDFMEMORY   Buffer,
	_In_ size_t      NumBytesTransferred,
	_In_ WDFCONTEXT  Context
)
{
	UNREFERENCED_PARAMETER(Pipe);

	WDFDEVICE       device;
	PDEVICE_CONTEXT pDeviceContext = Context;
	UCHAR*			pBuffer = NULL;
	NTSTATUS        status;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	device = WdfObjectContextGetObject(pDeviceContext);
	size_t headerSize = (unsigned int) pDeviceContext->DeviceInfo->tp_header;
	size_t fingerprintSize = (unsigned int) pDeviceContext->DeviceInfo->tp_fsize;

	if (NumBytesTransferred < headerSize || (NumBytesTransferred - headerSize) % fingerprintSize != 0) {

		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_DRIVER,
			"%!FUNC! Malformed input received. Length = %llu. Attempt to reset device.",
			NumBytesTransferred
		);

		status = AmtPtpEmergResetDevice(pDeviceContext);
		if (!NT_SUCCESS(status)) {

			TraceEvents(
				TRACE_LEVEL_INFORMATION,
				TRACE_DRIVER,
				"%!FUNC! AmtPtpEmergResetDevice failed with %!STATUS!",
				status
			);

		}

		return;
	}

	if (!pDeviceContext->IsWellspringModeOn) {

		TraceEvents(
			TRACE_LEVEL_WARNING,
			TRACE_DRIVER,
			"%!FUNC! Routine is called without enabling Wellspring mode"
		);

		return;
	}

	// Dispatch USB Interrupt routine by device family
	switch (pDeviceContext->DeviceInfo->tp_type) {
		case TYPE1:
		{
			TraceEvents(
				TRACE_LEVEL_WARNING,
				TRACE_DRIVER,
				"%!FUNC! Mode not yet supported"
			);
			break;
		}
		// Universal routine handler
		case TYPE2:
		case TYPE3:
		case TYPE4:
		{
			pBuffer = WdfMemoryGetBuffer(
				Buffer,
				NULL
			);

			status = AmtPtpServiceTouchInputInterrupt(
				pDeviceContext,
				pBuffer,
				NumBytesTransferred
			);

			if (!NT_SUCCESS(status)) {
				TraceEvents(
					TRACE_LEVEL_WARNING,
					TRACE_DRIVER,
					"%!FUNC! AmtPtpServiceTouchInputInterrupt failed with %!STATUS!",
					status
				);
			}
			break;
		}
		// Magic Trackpad 2
		case TYPE5:
		{
			pBuffer = WdfMemoryGetBuffer(
				Buffer,
				NULL
			);
			status = AmtPtpServiceTouchInputInterruptType5(
				pDeviceContext,
				pBuffer,
				NumBytesTransferred
			);

			if (!NT_SUCCESS(status)) {
				TraceEvents(
					TRACE_LEVEL_WARNING,
					TRACE_DRIVER,
					"%!FUNC! AmtPtpServiceTouchInputInterrupt5 failed with %!STATUS!",
					status
				);
			}
			break;
		}
	}

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);

}

_IRQL_requires_(PASSIVE_LEVEL)
BOOLEAN
AmtPtpEvtUsbInterruptReadersFailed(
	_In_ WDFUSBPIPE Pipe,
	_In_ NTSTATUS Status,
	_In_ USBD_STATUS UsbdStatus
)
{
	UNREFERENCED_PARAMETER(Pipe);
	UNREFERENCED_PARAMETER(UsbdStatus);
	UNREFERENCED_PARAMETER(Status);

	return TRUE;
}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AmtPtpServiceTouchInputInterrupt(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ UCHAR* Buffer,
	_In_ size_t NumBytesTransferred
)
{
	NTSTATUS Status;
	WDFREQUEST Request = NULL;
	WDFMEMORY  RequestMemory;
	PTP_REPORT PtpReport;
	LARGE_INTEGER CurrentPerfCounter;

	const struct TRACKPAD_FINGER *f;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	size_t raw_n, i = 0;
	size_t headerSize = (unsigned int) DeviceContext->DeviceInfo->tp_header;
	size_t fingerprintSize = (unsigned int) DeviceContext->DeviceInfo->tp_fsize;
	USHORT x = 0, y = 0;

	Status = STATUS_SUCCESS;
	// Reject incomplete packets before taking ownership of a pending HID read.
	if (DeviceContext->IsSurfaceReportOn &&
		(NumBytesTransferred < headerSize ||
		 fingerprintSize < (size_t)DeviceContext->DeviceInfo->tp_delta + sizeof(*f) ||
		 (NumBytesTransferred - headerSize) % fingerprintSize != 0)) {
		return STATUS_INVALID_BUFFER_SIZE;
	}
	if (DeviceContext->IsButtonReportOn &&
		NumBytesTransferred <= (size_t)DeviceContext->DeviceInfo->tp_button) {
		return STATUS_INVALID_BUFFER_SIZE;
	}
	RtlZeroMemory(&PtpReport, sizeof(PtpReport));
	PtpReport.ReportID = REPORTID_MULTITOUCH;
	PtpReport.IsButtonClicked = 0;

	// Retrieve next PTP touchpad request.
	Status = WdfIoQueueRetrieveNextRequest(
		DeviceContext->InputQueue,
		&Request
	);

	if (!NT_SUCCESS(Status)) {
		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_DRIVER,
			"%!FUNC! No pending PTP request. Interrupt disposed"
		);
		goto exit;
	}

	QueryPerformanceCounter(
		&CurrentPerfCounter
	);

	// Use the same advancing 100us counter as Type 5, including 16-bit wrap.
	AmtPtpSetScanTime(DeviceContext, &PtpReport, CurrentPerfCounter.QuadPart);

	// Allocate output memory.
	Status = WdfRequestRetrieveOutputMemory(
		Request,
		&RequestMemory
	);

	if (!NT_SUCCESS(Status)) {
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"%!FUNC! WdfRequestRetrieveOutputMemory failed with %!STATUS!",
			Status
		);
		goto exit;
	}

	// Type 2 touchpad surface report
	if (DeviceContext->IsSurfaceReportOn) {
		// Handles trackpad surface report here.
		raw_n = (NumBytesTransferred - headerSize) / fingerprintSize;
		if (raw_n >= PTP_MAX_CONTACT_POINTS) raw_n = PTP_MAX_CONTACT_POINTS;
		PtpReport.ContactCount = (UCHAR) raw_n;

#ifdef INPUT_CONTENT_TRACE
		TraceEvents(
			TRACE_LEVEL_INFORMATION,
			TRACE_DRIVER,
			"%!FUNC! with %llu points.",
			raw_n
		);
#endif

		// Fingers
		for (i = 0; i < raw_n; i++) {

			UCHAR *f_base = Buffer + headerSize + DeviceContext->DeviceInfo->tp_delta;
			f = (const struct TRACKPAD_FINGER*) (f_base + i * fingerprintSize);

			// Translate X and Y
			x = (AmtRawToInteger(f->abs_x) - DeviceContext->DeviceInfo->x.min) > 0 ? 
				((USHORT)(AmtRawToInteger(f->abs_x) - DeviceContext->DeviceInfo->x.min)) : 0;
			y = (DeviceContext->DeviceInfo->y.max - AmtRawToInteger(f->abs_y)) > 0 ? 
				((USHORT)(DeviceContext->DeviceInfo->y.max - AmtRawToInteger(f->abs_y))) : 0;

			// Defuzz functions remain the same
			// TODO: Implement defuzz later
			PtpReport.Contacts[i].ContactID = (UCHAR) i;
			PtpReport.Contacts[i].X = x;
			PtpReport.Contacts[i].Y = y;
			PtpReport.Contacts[i].TipSwitch = (AmtRawToInteger(f->touch_major) << 1) >= 200;
			PtpReport.Contacts[i].Confidence = (AmtRawToInteger(f->touch_minor) << 1) > 0;

#ifdef INPUT_CONTENT_TRACE
			TraceEvents(
				TRACE_LEVEL_INFORMATION,
				TRACE_INPUT,
				"%!FUNC!: Point %llu, X = %d, Y = %d, TipSwitch = %d, Confidence = %d, tMajor = %d, tMinor = %d, origin = %d, PTP Origin = %d",
				i,
				PtpReport.Contacts[i].X,
				PtpReport.Contacts[i].Y,
				PtpReport.Contacts[i].TipSwitch,
				PtpReport.Contacts[i].Confidence,
				AmtRawToInteger(f->touch_major) << 1,
				AmtRawToInteger(f->touch_minor) << 1,
				AmtRawToInteger(f->origin),
				(UCHAR) i
			);
#endif
		}
	}

	// Type 2 touchpad contains integrated trackpad buttons
	if (DeviceContext->IsButtonReportOn) {
		// Handles trackpad button input here.
		if (Buffer[DeviceContext->DeviceInfo->tp_button]) {
			PtpReport.IsButtonClicked = TRUE;
		}
	}

	// Compose final report and write it back
	Status = WdfMemoryCopyFromBuffer(
		RequestMemory,
		0,
		(PVOID) &PtpReport,
		sizeof(PTP_REPORT)
	);

	if (!NT_SUCCESS(Status)) {
		TraceEvents(
			TRACE_LEVEL_ERROR,
			TRACE_DRIVER,
			"%!FUNC! WdfMemoryCopyFromBuffer failed with %!STATUS!",
			Status
		);
		goto exit;
	}

	// Set result
	WdfRequestSetInformation(
		Request,
		sizeof(PTP_REPORT)
	);

exit:
	// Every dequeued request must complete, including output-buffer failures.
	if (Request != NULL) {
		WdfRequestComplete(Request, Status);
	}
	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);
	return Status;

}

_IRQL_requires_(PASSIVE_LEVEL)
NTSTATUS
AmtPtpServiceTouchInputInterruptType5(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ UCHAR* Buffer,
	_In_ size_t NumBytesTransferred
)
{
	PTP_REPORT PtpReport;
	LARGE_INTEGER CurrentPerfCounter;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	AcquireSRWLockExclusive(&DeviceContext->InterpolationLock);
	// Use the Bluetooth decoder too: button bit, bounds, contact capacity and
	// release ordering must not change when the cable is plugged in.
	if (!PtpFilterBuildType5Report(Buffer, NumBytesTransferred,
		(size_t)DeviceContext->DeviceInfo->tp_header,
		DeviceContext->DeviceInfo->x.min, DeviceContext->DeviceInfo->x.max,
		DeviceContext->DeviceInfo->y.min, DeviceContext->DeviceInfo->y.max, 0,
		(BOOLEAN)(DeviceContext->IsSurfaceReportOn != FALSE),
		(BOOLEAN)(DeviceContext->IsButtonReportOn != FALSE),
		&DeviceContext->LastPhysicalReport, &PtpReport)) {
		ReleaseSRWLockExclusive(&DeviceContext->InterpolationLock);
		return STATUS_INVALID_BUFFER_SIZE;
	}
	DeviceContext->LastPhysicalReport = PtpReport;
	// Continuous movement stays on the timer; transitions are delivered now.
	QueryPerformanceCounter(&CurrentPerfCounter);
	BOOLEAN transition = AmtPtpInterpOnSample(DeviceContext, &PtpReport, CurrentPerfCounter);
	TraceLoggingWrite(g_hAmtPtpDeviceTraceProvider, "PhysicalInput",
		TraceLoggingInt64(CurrentPerfCounter.QuadPart, "Qpc"),
		TraceLoggingBinary(Buffer, (ULONG)NumBytesTransferred, "Packet"));
	// Only contact/button transitions bypass the timer. Emitting every raw
	// movement here would reintroduce irregular spacing and prediction snaps.
	if (transition) {
		AmtPtpInterpBuildReport(DeviceContext, CurrentPerfCounter.QuadPart, &PtpReport);
		AmtPtpSetScanTime(DeviceContext, &PtpReport, CurrentPerfCounter.QuadPart);
		AmtPtpEmitReport(DeviceContext, &PtpReport, CurrentPerfCounter.QuadPart);
	}
	ReleaseSRWLockExclusive(&DeviceContext->InterpolationLock);

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);
	return STATUS_SUCCESS;
}

//
// Continuous motion is sampled on the timer cadence. A short fixed delay
// lets us interpolate actual samples rather than alternate prediction and
// correction. Raw positions and motion amplitude are never low-pass filtered.
//

static double
AmtPtpInterpAlpha(
	_In_ double CutoffHz,
	_In_ double DtSec
)
{
	double tau = 1.0 / (2.0 * 3.14159265358979323846 * CutoffHz);
	return 1.0 / (1.0 + tau / DtSec);
}

static BOOLEAN
AmtPtpInterpOnSample(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PPTP_REPORT Sample,
	_In_ LARGE_INTEGER Now
)
{
	double dtSec;
	BOOLEAN seen[INTERP_MAX_FINGERS];
	ULONG i;
	ULONG oldMask = 0, newMask = 0;
	BOOLEAN transition;

	if (DeviceContext->QpcFrequency <= 0) {
		return FALSE;
	}
	if (DeviceContext->InterpHasSample && Now.QuadPart - DeviceContext->LastSampleTime.QuadPart >=
		DeviceContext->QpcFrequency * INTERP_STALE_MS / 1000) {
		RtlZeroMemory(DeviceContext->InterpFingers, sizeof(DeviceContext->InterpFingers));
		DeviceContext->InterpHasSample = FALSE;
	}
	for (i = 0; i < INTERP_MAX_FINGERS; i++) {
		if (DeviceContext->InterpFingers[i].Active && DeviceContext->InterpFingers[i].TipSwitch)
			oldMask |= 1UL << i;
	}
	for (i = 0; i < Sample->ContactCount; i++) {
		if (Sample->Contacts[i].ContactID < INTERP_MAX_FINGERS && Sample->Contacts[i].TipSwitch)
			newMask |= 1UL << Sample->Contacts[i].ContactID;
	}
	transition = !DeviceContext->InterpHasSample || oldMask != newMask ||
		DeviceContext->InterpButton != Sample->IsButtonClicked;

	dtSec = (double)(Now.QuadPart - DeviceContext->LastSampleTime.QuadPart) /
			(double)DeviceContext->QpcFrequency;
	if (dtSec <= 0.0) {
		dtSec = 0.001;
	}

	RtlZeroMemory(seen, sizeof(seen));

	for (i = 0; i < Sample->ContactCount; i++) {
		PPTP_CONTACT c = &Sample->Contacts[i];
		ULONG id = c->ContactID;
		PINTERP_FINGER finger;

		if (id >= INTERP_MAX_FINGERS) {
			continue;
		}
		finger = &DeviceContext->InterpFingers[id];

		if (finger->Active && finger->TipSwitch && c->TipSwitch && dtSec < 0.2) {
			double a = AmtPtpInterpAlpha(INTERP_DCUTOFF, dtSec);
			double dx = ((double)c->X - finger->X) / dtSec;
			double dy = ((double)c->Y - finger->Y) / dtSec;
			finger->Vx = finger->Vx + a * (dx - finger->Vx);
			finger->Vy = finger->Vy + a * (dy - finger->Vy);
		} else {
			finger->Vx = 0.0;
			finger->Vy = 0.0;
			finger->HistoryCount = 0;
		}
		if (c->TipSwitch) {
			// Equal timestamps replace the latest sample rather than divide by zero.
			if (finger->HistoryCount &&
				Now.QuadPart <= finger->History[finger->HistoryCount - 1].Time) {
				finger->HistoryCount--;
			}
			if (finger->HistoryCount == INTERP_HISTORY_COUNT) {
				RtlMoveMemory(&finger->History[0], &finger->History[1],
					(INTERP_HISTORY_COUNT - 1) * sizeof(INTERP_POINT));
				finger->HistoryCount--;
			}
			PINTERP_POINT point = &finger->History[finger->HistoryCount++];
			point->X = (double)c->X;
			point->Y = (double)c->Y;
			point->Time = Now.QuadPart;
		}

		finger->X = (double)c->X;
		finger->Y = (double)c->Y;
		finger->Active = TRUE;
		finger->TipSwitch = c->TipSwitch;
		finger->Confidence = c->Confidence;
		seen[id] = TRUE;
	}

	// Deactivate fingers that are no longer reported.
	for (i = 0; i < INTERP_MAX_FINGERS; i++) {
		if (!seen[i]) {
			DeviceContext->InterpFingers[i].Active = FALSE;
		}
	}

	DeviceContext->InterpButton = Sample->IsButtonClicked;
	DeviceContext->LastSampleTime = Now;
	DeviceContext->InterpHasSample = TRUE;
	return transition;
}

static VOID
AmtPtpInterpPosition(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PINTERP_FINGER Finger,
	_In_ LONGLONG TargetQpc,
	_Out_ double* X,
	_Out_ double* Y
)
{
	ULONG i;
	*X = Finger->X;
	*Y = Finger->Y;
	if (!Finger->TipSwitch || !Finger->HistoryCount) return;
	PINTERP_POINT first = &Finger->History[0];
	if (TargetQpc <= first->Time) {
		*X = first->X;
		*Y = first->Y;
		return;
	}
	for (i = 1; i < Finger->HistoryCount; i++) {
		PINTERP_POINT a = &Finger->History[i - 1];
		PINTERP_POINT b = &Finger->History[i];
		if (TargetQpc <= b->Time) {
			double t = (double)(TargetQpc - a->Time) / (double)(b->Time - a->Time);
			*X = a->X + (b->X - a->X) * t;
			*Y = a->Y + (b->Y - a->Y) * t;
			return;
		}
	}
	PINTERP_POINT last = &Finger->History[Finger->HistoryCount - 1];
	LONGLONG elapsed = TargetQpc - last->Time;
	LONGLONG cap = DeviceContext->QpcFrequency * INTERP_MAX_EXTRAP_MS / 1000;
	if (elapsed > cap) elapsed = cap;
	double seconds = (double)elapsed / (double)DeviceContext->QpcFrequency;
	*X = last->X + Finger->Vx * seconds;
	*Y = last->Y + Finger->Vy * seconds;
}

static VOID
AmtPtpInterpBuildReport(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ LONGLONG NowQpc,
	_Out_ PPTP_REPORT Report
)
{
	LONGLONG targetQpc;
	ULONG i;
	ULONG count = 0;

	RtlZeroMemory(Report, sizeof(PTP_REPORT));
	Report->ReportID = REPORTID_MULTITOUCH;
	if (!DeviceContext->InterpHasSample || DeviceContext->QpcFrequency <= 0 ||
		NowQpc - DeviceContext->LastSampleTime.QuadPart >=
		DeviceContext->QpcFrequency * INTERP_STALE_MS / 1000) return;
	Report->IsButtonClicked = DeviceContext->InterpButton;

	if (!DeviceContext->IsSurfaceReportOn) {
		return;
	}

	targetQpc = NowQpc - DeviceContext->QpcFrequency * INTERP_DELAY_MS / 1000;

	for (i = 0; i < INTERP_MAX_FINGERS; i++) {
		PINTERP_FINGER finger = &DeviceContext->InterpFingers[i];
		PPTP_CONTACT c;
		double px, py;
		LONG xi, yi;

		if (!finger->Active) continue;

		AmtPtpInterpPosition(DeviceContext, finger, targetQpc, &px, &py);

		xi = (LONG)(px + 0.5);
		yi = (LONG)(py + 0.5);
		if (xi < 0) xi = 0;
		else if (xi > DeviceContext->DeviceInfo->x.max - DeviceContext->DeviceInfo->x.min)
			xi = DeviceContext->DeviceInfo->x.max - DeviceContext->DeviceInfo->x.min;
		if (yi < 0) yi = 0;
		else if (yi > DeviceContext->DeviceInfo->y.max - DeviceContext->DeviceInfo->y.min)
			yi = DeviceContext->DeviceInfo->y.max - DeviceContext->DeviceInfo->y.min;

		c = &Report->Contacts[count];
		c->ContactID = i;
		c->X = (USHORT)xi;
		c->Y = (USHORT)yi;
		c->TipSwitch = finger->TipSwitch;
		c->Confidence = finger->Confidence;
		count++;
		if (count >= PTP_MAX_CONTACT_POINTS) break;
	}

	Report->ContactCount = (UCHAR)count;
}

static VOID
AmtPtpSetScanTime(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PPTP_REPORT Report,
	_In_ LONGLONG NowQpc
)
{
	LONGLONG delta;

	if (DeviceContext->QpcFrequency <= 0) {
		Report->ScanTime = 0;
		return;
	}

	// HID ScanTime is an advancing 16-bit counter; LastEmitTime only gates ticks.
	delta = PtpFilterScanTicks(NowQpc - DeviceContext->PerfCounter.QuadPart,
		DeviceContext->QpcFrequency, -1);
	Report->ScanTime = (USHORT)delta;
}

static NTSTATUS
AmtPtpEmitReport(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PPTP_REPORT Report,
	_In_ LONGLONG NowQpc
)
{
	WDFREQUEST Request;
	WDFMEMORY RequestMemory;
	NTSTATUS Status;
	PTP_REPORT normalized = { 0 };
	BOOLEAN included[INTERP_MAX_FINGERS] = { 0 };
	ULONG i, j;

	normalized.ReportID = REPORTID_MULTITOUCH;
	normalized.ScanTime = Report->ScanTime;
	normalized.IsButtonClicked = Report->IsButtonClicked;
	// Keep existing contacts or release them exactly once, at the last
	// position actually delivered (which may have been a timer prediction).
	for (i = 0; i < DeviceContext->LastDeliveredReport.ContactCount; i++) {
		PTP_CONTACT c = DeviceContext->LastDeliveredReport.Contacts[i];
		if (!c.TipSwitch) continue;
		included[c.ContactID] = TRUE;
		for (j = 0; j < Report->ContactCount; j++) {
			if (Report->Contacts[j].ContactID == c.ContactID) break;
		}
		if (j < Report->ContactCount && Report->Contacts[j].TipSwitch) {
			c = Report->Contacts[j];
		} else {
			c.TipSwitch = FALSE;
		}
		normalized.Contacts[normalized.ContactCount++] = c;
	}
	for (i = 0; i < Report->ContactCount && normalized.ContactCount < PTP_MAX_CONTACT_POINTS; i++) {
		PTP_CONTACT c = Report->Contacts[i];
		if (c.ContactID < INTERP_MAX_FINGERS && !included[c.ContactID] && c.TipSwitch) {
			included[c.ContactID] = TRUE;
			normalized.Contacts[normalized.ContactCount++] = c;
		}
	}
	if (!normalized.ContactCount &&
		normalized.IsButtonClicked == DeviceContext->LastDeliveredReport.IsButtonClicked) {
		return STATUS_SUCCESS;
	}

	Status = WdfIoQueueRetrieveNextRequest(DeviceContext->InputQueue, &Request);
	if (!NT_SUCCESS(Status)) {
		TraceLoggingWrite(g_hAmtPtpDeviceTraceProvider, "InputReadUnavailable",
			TraceLoggingInt64(NowQpc, "Qpc"), TraceLoggingNTStatus(Status, "Status"));
		return Status;
	}

	Status = WdfRequestRetrieveOutputMemory(Request, &RequestMemory);
	if (!NT_SUCCESS(Status)) {
		WdfRequestComplete(Request, Status);
		return Status;
	}

	Status = WdfMemoryCopyFromBuffer(RequestMemory, 0, &normalized, sizeof(PTP_REPORT));
	if (!NT_SUCCESS(Status)) {
		WdfRequestComplete(Request, Status);
		return Status;
	}

	WdfRequestSetInformation(Request, sizeof(PTP_REPORT));
	DeviceContext->LastDeliveredReport = normalized;
	TraceLoggingWrite(g_hAmtPtpDeviceTraceProvider, "DeliveredInput",
		TraceLoggingInt64(NowQpc, "Qpc"),
		TraceLoggingBoolean(DeviceContext->InterpTimerEmission, "Timer"),
		TraceLoggingBinary(&normalized, sizeof(normalized), "Report"));
	WdfRequestComplete(Request, Status);

	DeviceContext->LastEmitTime.QuadPart = NowQpc;
	return Status;
}

VOID
AmtPtpInterpolationTick(
	_In_ PDEVICE_CONTEXT ctx
)
{
	PTP_REPORT report;
	LARGE_INTEGER now;
	LONGLONG sinceEmitMs;

	if (ctx == NULL) {
		return;
	}
	AcquireSRWLockExclusive(&ctx->InterpolationLock);
	if (!ctx->InterpHasSample) {
		ReleaseSRWLockExclusive(&ctx->InterpolationLock);
		return;
	}

	QueryPerformanceCounter(&now);

	// Emit at most once per timer period (125 Hz).
	if (ctx->QpcFrequency > 0) {
		sinceEmitMs = (now.QuadPart - ctx->LastEmitTime.QuadPart) * 1000 / ctx->QpcFrequency;
		if (sinceEmitMs < (LONGLONG)(INTERP_PERIOD_MS - 1)) {
			ReleaseSRWLockExclusive(&ctx->InterpolationLock);
			return;
		}
	}

	AmtPtpInterpBuildReport(ctx, now.QuadPart, &report);
	AmtPtpSetScanTime(ctx, &report, now.QuadPart);
	ctx->InterpTimerEmission = TRUE;
	AmtPtpEmitReport(ctx, &report, now.QuadPart);
	ctx->InterpTimerEmission = FALSE;
	ReleaseSRWLockExclusive(&ctx->InterpolationLock);
}

VOID
AmtPtpInterpolationTimerCallback(_In_ WDFTIMER Timer)
{
	WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
	AmtPtpInterpolationTick(DeviceGetContext(device));
}

// Helper function for numberic operation
static inline INT AmtRawToInteger(
	_In_ USHORT x
)
{
	return (signed short) x;
}
