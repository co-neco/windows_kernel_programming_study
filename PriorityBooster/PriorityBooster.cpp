#include <ntifs.h>
#include <ntddk.h>
#include "PriorityBoosterCommon.h"

#define SYM_LINK RTL_CONSTANT_STRING(L"\\??\\PriorityBooster")

// prototypes
void PriorityBoosterUnload(_In_ PDRIVER_OBJECT driverObject);
NTSTATUS PriorityBoosterCreateClose(_In_ PDEVICE_OBJECT deviceObject, _In_ PIRP irp);
NTSTATUS PriorityBoosterDeviceControl(_In_ PDEVICE_OBJECT deviceObject, _In_ PIRP irp);


// DriverEntry
extern "C" NTSTATUS
DriverEntry(_In_ PDRIVER_OBJECT driverObject, _In_ PUNICODE_STRING registryPath) {

	UNREFERENCED_PARAMETER(registryPath);

	driverObject->DriverUnload = PriorityBoosterUnload;
	driverObject->MajorFunction[IRP_MJ_CREATE] = PriorityBoosterCreateClose;
	driverObject->MajorFunction[IRP_MJ_CLOSE] = PriorityBoosterCreateClose;
	driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PriorityBoosterDeviceControl;

	UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\PriorityBooster");

	PDEVICE_OBJECT deviceObject;
	NTSTATUS status = IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create device obejct (0x%08X)\n", status));
		return status;
	}

	UNICODE_STRING symLink = SYM_LINK;
	status = IoCreateSymbolicLink(&symLink, &deviceName);
	if (!NT_SUCCESS(status)) {
		KdPrint(("Failed to create symbolic link (0x%08X)\n", status));
		IoDeleteDevice(deviceObject);
		return status;
	}

	return STATUS_SUCCESS;
}

void PriorityBoosterUnload(_In_ PDRIVER_OBJECT driverObject) {

	UNICODE_STRING symLink = SYM_LINK;
	IoDeleteSymbolicLink(&symLink);

	IoDeleteDevice(driverObject->DeviceObject);
}

_Use_decl_annotations_
NTSTATUS PriorityBoosterCreateClose(_In_ PDEVICE_OBJECT deviceObject, _In_ PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);
	
	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

_Use_decl_annotations_
NTSTATUS PriorityBoosterDeviceControl(_In_ PDEVICE_OBJECT deviceObject, _In_ PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);

	auto stack = IoGetCurrentIrpStackLocation(irp);
	NTSTATUS status = STATUS_SUCCESS;

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_PRIORITY_BOOSTER_SET_PRIORITY: {

		if (stack->Parameters.DeviceIoControl.InputBufferLength < sizeof(ThreadData)) {
			status = STATUS_BUFFER_TOO_SMALL;
			break;
		}

		auto data = (ThreadData*)stack->Parameters.DeviceIoControl.Type3InputBuffer;
		if (!data) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		if (data->priority < 1 || data->priority > 31) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		PETHREAD thread;
		status = PsLookupThreadByThreadId(ULongToHandle(data->threadId), &thread);
		if (!NT_SUCCESS(status))
			break;

		KeSetPriorityThread((PKTHREAD)thread, data->priority);
		ObDereferenceObject(thread);
		KdPrint(("Thread priority change for %d to %d succeeded!\n", data->threadId, data->priority));
		
		break;
	}
	default:
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}