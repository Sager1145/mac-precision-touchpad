// Detour.c: Windows HID detour facilities
//
// HIDCLASS replaces the transport driver's dispatch table, so internal HID
// IOCTLs sent to our lower device never reach HidBth. Instead of patching the
// shared driver object (which would affect every Bluetooth HID device), we
// route only our own requests through a private, unnamed control device that
// calls the transport's saved dispatch routine directly.

#include <Driver.h>
#include "Detour.tmh"

typedef struct _HID_PROXY_CONTEXT {
    PDEVICE_OBJECT   TransportDevice;
    PDRIVER_DISPATCH TransportDispatch;
} HID_PROXY_CONTEXT, *PHID_PROXY_CONTEXT;

WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(HID_PROXY_CONTEXT, PtpFilterGetProxyContext)

static EVT_WDFDEVICE_WDM_IRP_PREPROCESS PtpFilterProxyDispatch;

static NTSTATUS
PtpFilterProxyDispatch(
    _In_ WDFDEVICE Device,
    _Inout_ PIRP Irp
)
{
    PHID_PROXY_CONTEXT proxyContext = PtpFilterGetProxyContext(Device);
    PIO_STACK_LOCATION stack;

    // Hand our stack location to the transport, as IoCallDriver would. The
    // sender's completion routine stays in this location.
    IoSkipCurrentIrpStackLocation(Irp);
    IoSetNextIrpStackLocation(Irp);
    stack = IoGetCurrentIrpStackLocation(Irp);
    stack->DeviceObject = proxyContext->TransportDevice;
    return proxyContext->TransportDispatch(proxyContext->TransportDevice, Irp);
}

static NTSTATUS
PtpFilterCreateHidProxy(
    _In_ WDFDEVICE Device,
    _In_ PDEVICE_OBJECT TransportDevice,
    _In_ PDRIVER_DISPATCH TransportDispatch
)
{
    NTSTATUS status;
    PDEVICE_CONTEXT deviceContext = PtpFilterGetContext(Device);
    PWDFDEVICE_INIT proxyInit;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_IO_TARGET_OPEN_PARAMS openParams;
    WDFDEVICE proxy = NULL;
    WDFIOTARGET target = NULL;
    PHID_PROXY_CONTEXT proxyContext;
    PDEVICE_OBJECT proxyDeviceObject;
    // Kernel only; the device is also unnamed, so user mode can never open it.
    DECLARE_CONST_UNICODE_STRING(proxySddl, L"D:P(A;;GA;;;SY)");

    PAGED_CODE();

    proxyInit = WdfControlDeviceInitAllocate(WdfDeviceGetDriver(Device), &proxySddl);
    if (proxyInit == NULL) {
        status = STATUS_INSUFFICIENT_RESOURCES;
        goto exit;
    }

    status = WdfDeviceInitAssignWdmIrpPreprocessCallback(proxyInit, PtpFilterProxyDispatch,
        IRP_MJ_INTERNAL_DEVICE_CONTROL, NULL, 0);
    if (!NT_SUCCESS(status)) {
        WdfDeviceInitFree(proxyInit);
        goto exit;
    }

    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, HID_PROXY_CONTEXT);
    status = WdfDeviceCreate(&proxyInit, &attributes, &proxy);
    if (!NT_SUCCESS(status)) {
        if (proxyInit != NULL) WdfDeviceInitFree(proxyInit);
        proxy = NULL;
        goto exit;
    }

    proxyContext = PtpFilterGetProxyContext(proxy);
    proxyContext->TransportDevice = TransportDevice;
    proxyContext->TransportDispatch = TransportDispatch;

    // The transport reuses the proxy's stack location, so senders need exactly
    // as many locations as a direct call to the transport.
    proxyDeviceObject = WdfDeviceWdmGetDeviceObject(proxy);
    proxyDeviceObject->StackSize = TransportDevice->StackSize;
    WdfControlFinishInitializing(proxy);

    WDF_OBJECT_ATTRIBUTES_INIT(&attributes);
    attributes.ParentObject = Device;
    status = WdfIoTargetCreate(Device, &attributes, &target);
    if (!NT_SUCCESS(status)) {
        target = NULL;
        goto exit;
    }

    WDF_IO_TARGET_OPEN_PARAMS_INIT_EXISTING_DEVICE(&openParams, proxyDeviceObject);
    status = WdfIoTargetOpen(target, &openParams);
    if (!NT_SUCCESS(status)) goto exit;

    deviceContext->HidProxyDevice = proxy;
    deviceContext->HidIoTarget = target;
    proxy = NULL;
    target = NULL;

exit:
    if (target != NULL) WdfObjectDelete(target);
    if (proxy != NULL) WdfObjectDelete(proxy);
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit, Status = %!STATUS!", status);
    return status;
}

NTSTATUS
PtpFilterDetourWindowsHIDStack(
    _In_ WDFDEVICE Device
)
{
    NTSTATUS status = STATUS_SUCCESS;
    PDEVICE_CONTEXT deviceContext;
    PDEVICE_OBJECT  hidTransportWdmDeviceObject = NULL;
    PDRIVER_OBJECT  hidTransportWdmDriverObject = NULL;
    PIO_CLIENT_EXTENSION hidTransportIoClientExtension = NULL;
    PHIDCLASS_DRIVER_EXTENSION hidTransportClassExtension = NULL;
    PDRIVER_DISPATCH transportDispatch;

    PAGED_CODE();
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Entry");
    deviceContext = PtpFilterGetContext(Device);

    if (deviceContext->WdmDeviceObject == NULL || deviceContext->WdmDeviceObject->DriverObject == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! WDM Device Object or Driver Object is null, can't continue");
        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    // Access the driver object to find next low-level device (in our case, we expect it to be HID transport driver)
    hidTransportWdmDeviceObject = IoGetLowerDeviceObject(deviceContext->WdmDeviceObject);
    if (hidTransportWdmDeviceObject == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! IoGetLowerDeviceObject returns null, can't continue");
        status = STATUS_UNSUCCESSFUL;
        goto exit;
    }

    hidTransportWdmDriverObject = hidTransportWdmDeviceObject->DriverObject;
    if (hidTransportWdmDriverObject == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! DriverObject of HID transport Device Object is null, can't continue");
        status = STATUS_UNSUCCESSFUL;
        goto cleanup;
    }

    // Verify if the driver extension is what we expected.
    if (hidTransportWdmDriverObject->DriverExtension == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! DriverExtension of HID transport Driver Object is null, can't continue");
        status = STATUS_UNSUCCESSFUL;
        goto cleanup;
    }

    // Just two more check...
    hidTransportIoClientExtension = ((PDRIVER_EXTENSION_EXT)hidTransportWdmDriverObject->DriverExtension)->IoClientExtension;
    if (hidTransportIoClientExtension == NULL) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! IO Extension is NULL, can't continue");
        status = STATUS_UNSUCCESSFUL;
        goto cleanup;
    }

    if (strncmp(HID_CLASS_EXTENSION_LITERAL_ID, hidTransportIoClientExtension->ClientIdentificationAddress, sizeof(HID_CLASS_EXTENSION_LITERAL_ID)) != 0) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! IO Extension mistmatch, can't continue");
        status = STATUS_UNSUCCESSFUL;
        goto cleanup;
    }

    // HIDCLASS keeps the transport's original dispatch table here. Read it,
    // never write the driver object.
    hidTransportClassExtension = (PHIDCLASS_DRIVER_EXTENSION)(hidTransportIoClientExtension + 1);

    // This layout is private to HIDCLASS. Before calling a pointer read from
    // it, check that the structure points back at this driver and that the
    // handler lies inside its image; otherwise fail start instead of jumping
    // to an arbitrary address after a Windows update.
    if (hidTransportClassExtension->MinidriverObject != hidTransportWdmDriverObject) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! HIDCLASS extension layout not recognized, can't continue");
        status = STATUS_NOT_SUPPORTED;
        goto cleanup;
    }

    transportDispatch = hidTransportClassExtension->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL];
    if (transportDispatch == NULL ||
        hidTransportWdmDriverObject->DriverStart == NULL ||
        (ULONG_PTR)transportDispatch < (ULONG_PTR)hidTransportWdmDriverObject->DriverStart ||
        (ULONG_PTR)transportDispatch >= (ULONG_PTR)hidTransportWdmDriverObject->DriverStart + hidTransportWdmDriverObject->DriverSize) {
        TraceEvents(TRACE_LEVEL_ERROR, TRACE_DEVICE, "%!FUNC! Transport internal IOCTL handler is not inside the transport driver, can't continue");
        status = STATUS_NOT_SUPPORTED;
        goto cleanup;
    }

    status = PtpFilterCreateHidProxy(Device, hidTransportWdmDeviceObject, transportDispatch);
    if (!NT_SUCCESS(status)) goto cleanup;

    // The proxy keeps the reference until SelfManagedIoCleanup.
    deviceContext->HidTransportDevice = hidTransportWdmDeviceObject;
    hidTransportWdmDeviceObject = NULL;
    deviceContext->IsHidIoDetourCompleted = TRUE;
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Private HID transport route ready");

cleanup:
    if (hidTransportWdmDeviceObject != NULL) {
        ObDereferenceObject(hidTransportWdmDeviceObject);
    }
exit:
    TraceEvents(TRACE_LEVEL_INFORMATION, TRACE_DEVICE, "%!FUNC! Exit, Status = %!STATUS!", status);
    return status;
}

VOID
PtpFilterReleaseWindowsHIDStack(
    _In_ WDFDEVICE Device
)
{
    PDEVICE_CONTEXT deviceContext = PtpFilterGetContext(Device);

    PAGED_CODE();

    // Delete the target before the proxy device it points at.
    if (deviceContext->HidIoTarget != NULL) {
        WdfObjectDelete(deviceContext->HidIoTarget);
        deviceContext->HidIoTarget = NULL;
    }
    if (deviceContext->HidProxyDevice != NULL) {
        WdfObjectDelete(deviceContext->HidProxyDevice);
        deviceContext->HidProxyDevice = NULL;
    }
    if (deviceContext->HidTransportDevice != NULL) {
        ObDereferenceObject(deviceContext->HidTransportDevice);
        deviceContext->HidTransportDevice = NULL;
    }
    deviceContext->IsHidIoDetourCompleted = FALSE;
}
