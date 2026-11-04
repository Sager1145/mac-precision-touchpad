#include <driver.h>
#define TraceLoggingWrite(...) ((void)0)
void WdfRequestComplete(WDFREQUEST request, NTSTATUS status);
void WdfRequestSetInformation(WDFREQUEST request, ULONG_PTR bytes);
#include "interpolation-under-test.inc"
#include <assert.h>
#include <math.h>
#include <stdio.h>
struct TEST_REQUEST { PTP_REPORT report; ULONG_PTR bytes; } testRequest;
static int requestAvailable=1, completed=0;
NTSTATUS WdfIoQueueRetrieveNextRequest(WDFQUEUE queue, WDFREQUEST *request) {
 (void)queue; if(!requestAvailable) return STATUS_NO_MORE_ENTRIES;
 *request=&testRequest; return STATUS_SUCCESS;
}
NTSTATUS WdfRequestRetrieveOutputMemory(WDFREQUEST request, WDFMEMORY *memory) { *memory=request; return STATUS_SUCCESS; }
NTSTATUS WdfMemoryCopyFromBuffer(WDFMEMORY memory,size_t offset,void *buffer,size_t length) {
 (void)offset; memcpy(&memory->report,buffer,length); return STATUS_SUCCESS;
}
void WdfRequestSetInformation(WDFREQUEST request, ULONG_PTR bytes) { request->bytes=bytes; }
void WdfRequestComplete(WDFREQUEST request, NTSTATUS status) { (void)request; assert(NT_SUCCESS(status)); completed++; }
static void testProtocol(void) {
 DEVICE_CONTEXT ctx={0}; PTP_REPORT report={0};
 report.ReportID=REPORTID_MULTITOUCH; report.ContactCount=1;
 report.Contacts[0].ContactID=7; report.Contacts[0].TipSwitch=1;
 report.Contacts[0].X=1000; report.Contacts[0].Y=1200;
 assert(NT_SUCCESS(AmtPtpEmitReport(&ctx,&report,100)));
 report.Contacts[0].X=1050; ctx.InterpTimerEmission=TRUE;
 assert(NT_SUCCESS(AmtPtpEmitReport(&ctx,&report,200)));
 report.Contacts[0].TipSwitch=0; report.Contacts[0].X=0;
 assert(NT_SUCCESS(AmtPtpEmitReport(&ctx,&report,300)));
 assert(testRequest.report.Contacts[0].X==1050 && !testRequest.report.Contacts[0].TipSwitch);
 assert(completed==3);
 AmtPtpEmitReport(&ctx,&report,400); assert(completed==3);
 report.Contacts[0].TipSwitch=1; AmtPtpEmitReport(&ctx,&report,500);
 report.ContactCount=0; AmtPtpEmitReport(&ctx,&report,600);
 assert(completed==5 && testRequest.report.ContactCount==1 && !testRequest.report.Contacts[0].TipSwitch);
 requestAvailable=0; report.ContactCount=1;
 assert(!NT_SUCCESS(AmtPtpEmitReport(&ctx,&report,700)));
 assert(!ctx.LastDeliveredReport.Contacts[0].TipSwitch);
}
int main(void) {
 testProtocol();
 DEVICE_CONTEXT ctx = {0};
 PTP_REPORT sample = {0}, output;
 LARGE_INTEGER now;
 ctx.DeviceInfo = &Bcm5974ConfigTable[0];
 for (const struct BCM5974_CONFIG *cfg=Bcm5974ConfigTable;cfg->ansi;cfg++)
  if(cfg->ansi==USB_DEVICE_ID_APPLE_MAGICTRACKPAD2_USBC) ctx.DeviceInfo=cfg;
 ctx.QpcFrequency=10000000; ctx.IsSurfaceReportOn=TRUE;
 sample.ContactCount=1; sample.Contacts[0].ContactID=15;
 sample.Contacts[0].X=1000; sample.Contacts[0].Y=1000;
 sample.Contacts[0].TipSwitch=1; sample.Contacts[0].Confidence=1;
 now.QuadPart=150000; AmtPtpInterpOnSample(&ctx,&sample,now);
 assert(ctx.InterpFingers[15].Vx==0);
 sample.Contacts[0].X=1150; sample.Contacts[0].Y=1300;
 now.QuadPart=300000; AmtPtpInterpOnSample(&ctx,&sample,now);
 double alpha=1.0/(1.0+1.0/(2.0*3.14159265358979323846*15.0)/0.015);
 assert(fabs(ctx.InterpFingers[15].Vx-alpha*10000.0)<0.0001);
 assert(ctx.InterpFingers[15].X==1150 && ctx.InterpFingers[15].Y==1300);
 AmtPtpInterpBuildReport(&ctx,300000,&output);
 assert(output.Contacts[0].X==1150 && output.Contacts[0].Y==1300);
 AmtPtpInterpBuildReport(&ctx,380000,&output);
 assert(output.Contacts[0].X==(USHORT)(1150+alpha*10000.0*0.008));
 assert(output.Contacts[0].Y==(USHORT)(1300+alpha*20000.0*0.008));
 AmtPtpInterpBuildReport(&ctx,900000,&output);
 assert(output.Contacts[0].X==(USHORT)(1150+alpha*10000.0*0.015));
 ctx.LastEmitTime.QuadPart=300000; AmtPtpSetScanTime(&ctx,&output,380000);
 assert(output.ScanTime==380);
 sample.Contacts[0].TipSwitch=0; now.QuadPart=450000;
 AmtPtpInterpOnSample(&ctx,&sample,now);
 AmtPtpInterpBuildReport(&ctx,530000,&output);
 assert(output.Contacts[0].X==1150 && !output.Contacts[0].TipSwitch);
 sample.Contacts[0].TipSwitch=1; sample.Contacts[0].X=2500; now.QuadPart=600000;
 AmtPtpInterpOnSample(&ctx,&sample,now); assert(ctx.InterpFingers[15].Vx==0);
 sample.ContactCount=0; now.QuadPart=750000; AmtPtpInterpOnSample(&ctx,&sample,now);
 assert(!ctx.InterpFingers[15].Active);
 AmtPtpInterpBuildReport(&ctx,830000,&output); assert(output.ContactCount==0);
 puts("PASS: raw positions, velocity-only LPF, 8ms prediction, 15ms cap, advancing ScanTime + one-shot liftoff, liftoff, ID reuse, disappearance");
 return 0;
}