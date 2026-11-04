// InputInterrupt.c: Handles device input event

#include <driver.h>
#include "InputInterrupt.tmh"

//
// Forward declarations for the interpolation helpers used by the Type 5
// handler below. They are defined later in this file (spliced from interp-new.c).
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

static VOID
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
	WDFREQUEST Request;
	WDFMEMORY  RequestMemory;
	PTP_REPORT PtpReport;
	LARGE_INTEGER CurrentPerfCounter;
	LONGLONG PerfCounterDelta;

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

	// Scan time is in 100us
	PerfCounterDelta = (CurrentPerfCounter.QuadPart - DeviceContext->PerfCounter.QuadPart) / 100;
	DeviceContext->PerfCounter = CurrentPerfCounter;
	// Only two bytes allocated
	if (PerfCounterDelta > 0xFF)
	{
		PerfCounterDelta = 0xFF;
	}

	PtpReport.ScanTime = (USHORT) PerfCounterDelta;

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

	// Set completion flag
	WdfRequestComplete(
		Request,
		Status
	);

exit:
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

	const struct TRACKPAD_FINGER *f;
	const struct TRACKPAD_FINGER_TYPE5 *f_type5;

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Entry"
	);

	RtlZeroMemory(&PtpReport, sizeof(PTP_REPORT));
	PtpReport.ReportID = REPORTID_MULTITOUCH;
	PtpReport.IsButtonClicked = 0;

	INT x, y = 0;
	size_t raw_n, i = 0;
	size_t headerSize = (unsigned int) DeviceContext->DeviceInfo->tp_header;
	size_t fingerprintSize = (unsigned int) DeviceContext->DeviceInfo->tp_fsize;

	// Type 5 finger report
	if (DeviceContext->IsSurfaceReportOn) {
		raw_n = (NumBytesTransferred - headerSize) / fingerprintSize;
		if (raw_n >= PTP_MAX_CONTACT_POINTS) raw_n = PTP_MAX_CONTACT_POINTS;
		PtpReport.ContactCount = (UCHAR)raw_n;

		// Fingers to array
		for (i = 0; i < raw_n; i++) {
			UCHAR *f_base = Buffer + headerSize + DeviceContext->DeviceInfo->tp_delta;
			f = (const struct TRACKPAD_FINGER*) (f_base + i * fingerprintSize);
			f_type5 = (const struct TRACKPAD_FINGER_TYPE5*) f;

			USHORT tmp_x = (*((USHORT*)f_type5)) & 0x1fff;
			UINT tmp_y = (INT)(*((UINT*)f_type5));

			x = (SHORT) (tmp_x << 3) >> 3;
			y = -(INT) (tmp_y << 6) >> 19;

			x = (x - DeviceContext->DeviceInfo->x.min) > 0 ? (x - DeviceContext->DeviceInfo->x.min) : 0;
			y = (y - DeviceContext->DeviceInfo->y.min) > 0 ? (y - DeviceContext->DeviceInfo->y.min) : 0;

			PtpReport.Contacts[i].ContactID = f_type5->ContactIdentifier.Id;
			PtpReport.Contacts[i].X = (USHORT) x;
			PtpReport.Contacts[i].Y = (USHORT) y;
			// Use the dedicated touch-state bits (high two bits of AbsoluteY[1]):
			// state == 0x80 means the finger is in contact. The old code used the
			// touch-major axis as a proxy, which fails to report the lift cleanly
			// and breaks flick detection / inertial scrolling.
			PtpReport.Contacts[i].TipSwitch = ((f_type5->AbsoluteY[1] & 0xC0) == 0x80);
			PtpReport.Contacts[i].Confidence = TRUE;
		}
	}

	// Button
	if (DeviceContext->IsButtonReportOn) {
		if (Buffer[DeviceContext->DeviceInfo->tp_button]) {
			PtpReport.IsButtonClicked = TRUE;
		}
	}

	// Feed the raw sample into the 1-euro filter, then emit the filtered position
	// immediately (no extra latency). The periodic timer resamples the filtered
	// position + smoothed velocity between physical samples.
	QueryPerformanceCounter(&CurrentPerfCounter);
	AmtPtpInterpOnSample(DeviceContext, &PtpReport, CurrentPerfCounter);
	AmtPtpInterpBuildReport(DeviceContext, CurrentPerfCounter.QuadPart, &PtpReport);
	AmtPtpSetScanTime(DeviceContext, &PtpReport, CurrentPerfCounter.QuadPart);
	AmtPtpEmitReport(DeviceContext, &PtpReport, CurrentPerfCounter.QuadPart);

	TraceEvents(
		TRACE_LEVEL_INFORMATION,
		TRACE_DRIVER,
		"%!FUNC! Exit"
	);
	return STATUS_SUCCESS;
}

//
// Motion smoothing using the "1-euro filter" (Casiez et al., 2012) - an adaptive
// low-pass filter used by Android's input pipeline and widely deployed for
// noisy touch/pointer input:
//   * slow movement  -> low cutoff  -> strong jitter removal
//   * fast movement  -> high cutoff -> minimal lag
// We also low-pass the derived velocity, then extrapolate a short, clamped
// distance using that smoothed velocity. Every emitted position comes from the
// filter, so there is no "snap back" to raw coordinates between reports.
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

static VOID
AmtPtpInterpFilterFinger(
	_Inout_ PINTERP_FINGER Finger,
	_In_ double X,
	_In_ double Y,
	_In_ double DtSec
)
{
	double dx, dy, speed, cutoff, a;

	if (DtSec <= 0 || !Finger->FilterInit) {
		Finger->Xf = X;
		Finger->Yf = Y;
		Finger->Dxf = 0.0;
		Finger->Dyf = 0.0;
		Finger->XPrev = X;
		Finger->YPrev = Y;
		Finger->FilterInit = TRUE;
		return;
	}

	// Raw derivative (velocity).
	dx = (X - Finger->XPrev) / DtSec;
	dy = (Y - Finger->YPrev) / DtSec;

	// Low-pass the derivative to get a stable velocity.
	a = AmtPtpInterpAlpha(INTERP_DCUTOFF, DtSec);
	Finger->Dxf = Finger->Dxf + a * (dx - Finger->Dxf);
	Finger->Dyf = Finger->Dyf + a * (dy - Finger->Dyf);

	// Adaptive cutoff: higher speed -> less smoothing (less lag).
	speed = (Finger->Dxf < 0 ? -Finger->Dxf : Finger->Dxf) +
			(Finger->Dyf < 0 ? -Finger->Dyf : Finger->Dyf);
	cutoff = INTERP_MIN_CUTOFF + INTERP_BETA * speed;
	a = AmtPtpInterpAlpha(cutoff, DtSec);

	// Low-pass the position.
	Finger->Xf = Finger->Xf + a * (X - Finger->Xf);
	Finger->Yf = Finger->Yf + a * (Y - Finger->Yf);

	Finger->XPrev = X;
	Finger->YPrev = Y;
}

static VOID
AmtPtpInterpOnSample(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ PPTP_REPORT Sample,
	_In_ LARGE_INTEGER Now
)
{
	double dtSec;
	BOOLEAN seen[INTERP_MAX_FINGERS];
	ULONG i;

	if (DeviceContext->QpcFrequency <= 0) {
		return;
	}

	dtSec = (double)(Now.QuadPart - DeviceContext->LastSampleTime.QuadPart) /
			(double)DeviceContext->QpcFrequency;

	RtlZeroMemory(seen, sizeof(seen));

	for (i = 0; i < Sample->ContactCount; i++) {
		PPTP_CONTACT c = &Sample->Contacts[i];
		ULONG id = c->ContactID;
		PINTERP_FINGER finger;

		if (id >= INTERP_MAX_FINGERS) {
			continue;
		}
		finger = &DeviceContext->InterpFingers[id];

		// New touch (finger was not active in the previous sample): reset filter.
		if (!finger->Active) {
			finger->FilterInit = FALSE;
		}

		AmtPtpInterpFilterFinger(finger, (double)c->X, (double)c->Y, dtSec);

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
}

static VOID
AmtPtpInterpBuildReport(
	_In_ PDEVICE_CONTEXT DeviceContext,
	_In_ LONGLONG NowQpc,
	_Out_ PPTP_REPORT Report
)
{
	LONGLONG elapsedQpc, maxQpc;
	double extrapSec;
	ULONG i;
	ULONG count = 0;

	RtlZeroMemory(Report, sizeof(PTP_REPORT));
	Report->ReportID = REPORTID_MULTITOUCH;
	Report->IsButtonClicked = DeviceContext->InterpButton;

	if (!DeviceContext->IsSurfaceReportOn || !DeviceContext->InterpHasSample ||
		DeviceContext->QpcFrequency <= 0) {
		return;
	}

	elapsedQpc = NowQpc - DeviceContext->LastSampleTime.QuadPart;
	if (elapsedQpc < 0) elapsedQpc = 0;
	maxQpc = (LONGLONG)DeviceContext->QpcFrequency * INTERP_MAX_EXTRAP_MS / 1000;
	if (elapsedQpc > maxQpc) elapsedQpc = maxQpc;
	extrapSec = (double)elapsedQpc / (double)DeviceContext->QpcFrequency;

	for (i = 0; i < INTERP_MAX_FINGERS; i++) {
		PINTERP_FINGER finger = &DeviceContext->InterpFingers[i];
		PPTP_CONTACT c;
		double px, py;
		LONG xi, yi;

		if (!finger->Active) continue;

		px = finger->Xf + finger->Dxf * extrapSec;
		py = finger->Yf + finger->Dyf * extrapSec;

		xi = (LONG)px;
		yi = (LONG)py;
		if (xi < 0) xi = 0;
		else if (xi > 0xFFFF) xi = 0xFFFF;
		if (yi < 0) yi = 0;
		else if (yi > 0xFFFF) yi = 0xFFFF;

		c = &Report->Contacts[count];
		c->ContactID = i;   // index == hardware ContactID (0..15)
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

	delta = (NowQpc - DeviceContext->LastEmitTime.QuadPart) * 10000 / DeviceContext->QpcFrequency;
	if (delta < 0) delta = 0;
	if (delta > 0xFFFF) delta = 0xFFFF;
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

	Status = WdfIoQueueRetrieveNextRequest(DeviceContext->InputQueue, &Request);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	Status = WdfRequestRetrieveOutputMemory(Request, &RequestMemory);
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	Status = WdfMemoryCopyFromBuffer(RequestMemory, 0, (PVOID)Report, sizeof(PTP_REPORT));
	if (!NT_SUCCESS(Status)) {
		return Status;
	}

	WdfRequestSetInformation(Request, sizeof(PTP_REPORT));
	WdfRequestComplete(Request, Status);

	DeviceContext->LastEmitTime.QuadPart = NowQpc;
	return Status;
}

VOID
AmtPtpInterpolationTimerCallback(
	_In_ WDFTIMER Timer
)
{
	WDFDEVICE device = (WDFDEVICE)WdfTimerGetParentObject(Timer);
	PDEVICE_CONTEXT ctx = DeviceGetContext(device);
	PTP_REPORT report;
	LARGE_INTEGER now;
	LONGLONG sinceEmitMs;

	if (ctx == NULL || !ctx->InterpHasSample) {
		return;
	}

	QueryPerformanceCounter(&now);

	// Emit at most once per timer period (125 Hz).
	if (ctx->QpcFrequency > 0) {
		sinceEmitMs = (now.QuadPart - ctx->LastEmitTime.QuadPart) * 1000 / ctx->QpcFrequency;
		if (sinceEmitMs < (LONGLONG)(INTERP_PERIOD_MS - 1)) {
			return;
		}
	}

	AmtPtpInterpBuildReport(ctx, now.QuadPart, &report);
	AmtPtpSetScanTime(ctx, &report, now.QuadPart);
	AmtPtpEmitReport(ctx, &report, now.QuadPart);
}

// Helper function for numberic operation
static inline INT AmtRawToInteger(
	_In_ USHORT x
)
{
	return (signed short) x;
}
