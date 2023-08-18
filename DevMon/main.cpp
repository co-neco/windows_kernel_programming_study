#include "DevMonManager.h"
#include "common.h"

DevMonManager g_devMonManager;

DRIVER_UNLOAD DevMonUnload;
DRIVER_DISPATCH HandleFilterFunction, DevMonDeviceControl;

NTSTATUS CompleteRequest(PIRP irp, NTSTATUS status = STATUS_SUCCESS);
const char* MajorFunctionToString(UCHAR majorFunction);

extern "C" NTSTATUS
DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\KDevMon");
	PDEVICE_OBJECT deviceObject = nullptr;

	auto status = IoCreateDevice(driverObject, 0, &devName, FILE_DEVICE_UNKNOWN, 0, FALSE, &deviceObject);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "Failed to create the main device object"));
		return status;
	}

	UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\KDevMon");
	status = IoCreateSymbolicLink(&linkName, &devName);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "Failed to create the main device object's symbolic name"));
		return status;
	}

	driverObject->DriverUnload = DevMonUnload;

	for (auto& func : driverObject->MajorFunction)
		func = HandleFilterFunction;

	g_devMonManager.Init(driverObject, deviceObject);
	return status;
}

void DevMonUnload(PDRIVER_OBJECT driverObject) {

	UNICODE_STRING linkName = RTL_CONSTANT_STRING(L"\\??\\KDevMon");
	IoDeleteSymbolicLink(&linkName);

	UNICODE_STRING devName = RTL_CONSTANT_STRING(L"\\Device\\KDevMon");
	IoDeleteDevice(driverObject->DeviceObject);

	g_devMonManager.RemoveAllDevices();
}

NTSTATUS HandleFilterFunction(PDEVICE_OBJECT deviceObject, PIRP irp) {

	if (deviceObject == g_devMonManager.GetCDO()) {
		switch (IoGetCurrentIrpStackLocation(irp)->MajorFunction) {
		case IRP_MJ_CREATE:
		case IRP_MJ_CLOSE:
			return CompleteRequest(irp);
		case IRP_MJ_DEVICE_CONTROL:
			return DevMonDeviceControl(deviceObject, irp);
		default:
			return CompleteRequest(irp);
		}
	}

	auto ext = (DeviceExtension*)deviceObject->DeviceExtension;
	auto thread = irp->Tail.Overlay.Thread;

	HANDLE tid = nullptr, pid = nullptr;
	if (thread) {
		tid = PsGetThreadId(thread);
		pid = PsGetThreadProcessId(thread);
	}

	auto stack = IoGetCurrentIrpStackLocation(irp);
	DbgPrint(DRIVER_PREFIX "Intercepted driver: %wZ, PID: %d, TID: %d, MJ=%d (%s)",
		&ext->lowerDeviceObject->DriverObject->DriverName, HandleToUlong(pid), HandleToUlong(tid),
		stack->MajorFunction, MajorFunctionToString(stack->MajorFunction));

	IoSkipCurrentIrpStackLocation(irp);
	return IoCallDriver(ext->lowerDeviceObject, irp);
}

NTSTATUS DevMonDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);

	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto code = stack->Parameters.DeviceIoControl.IoControlCode;
	auto status = STATUS_SUCCESS;

	switch (code) {
	case IOCTL_DEVMON_ADD_DEVICE:
	case IOCTL_DEVMON_REMOVE_DEVICE: {

		auto buffer = (WCHAR*)irp->AssociatedIrp.SystemBuffer;
		auto len = stack->Parameters.DeviceIoControl.InputBufferLength;
		if (!buffer || len > 512) {
			status = STATUS_INVALID_BUFFER_SIZE;
			break;
		}

		buffer[len / sizeof(WCHAR) - 1] = L'\0';
		if (code == IOCTL_DEVMON_ADD_DEVICE)
			status = g_devMonManager.AddDevice(buffer);
		else
			status = g_devMonManager.RemoveDevice(buffer) ? STATUS_SUCCESS : STATUS_NOT_FOUND;
		break;
	}
	case IOCTL_DEVMON_REMOVE_ALL_DEVICE: {
		g_devMonManager.RemoveAllDevices();
		break;
	}
	}

	CompleteRequest(irp, status);
	return status;
}

NTSTATUS CompleteRequest(PIRP irp, NTSTATUS status) {
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return status;
}

const char* MajorFunctionToString(UCHAR majorFunction) {

static const char* strings[] = {
		"IRP_MJ_CREATE",
		"IRP_MJ_CREATE_NAMED_PIPE",
		"IRP_MJ_CLOSE",
		"IRP_MJ_READ",
		"IRP_MJ_WRITE",
		"IRP_MJ_QUERY_INFORMATION",
		"IRP_MJ_SET_INFORMATION",
		"IRP_MJ_QUERY_EA",
		"IRP_MJ_SET_EA",
		"IRP_MJ_FLUSH_BUFFERS",
		"IRP_MJ_QUERY_VOLUME_INFORMATION",
		"IRP_MJ_SET_VOLUME_INFORMATION",
		"IRP_MJ_DIRECTORY_CONTROL",
		"IRP_MJ_FILE_SYSTEM_CONTROL",
		"IRP_MJ_DEVICE_CONTROL",
		"IRP_MJ_INTERNAL_DEVICE_CONTROL",
		"IRP_MJ_SHUTDOWN",
		"IRP_MJ_LOCK_CONTROL",
		"IRP_MJ_CLEANUP",
		"IRP_MJ_CREATE_MAILSLOT",
		"IRP_MJ_QUERY_SECURITY",
		"IRP_MJ_SET_SECURITY",
		"IRP_MJ_POWER",
		"IRP_MJ_SYSTEM_CONTROL",
		"IRP_MJ_DEVICE_CHANGE",
		"IRP_MJ_QUERY_QUOTA",
		"IRP_MJ_SET_QUOTA",
		"IRP_MJ_PNP",
	};

	NT_ASSERT(majorFunction <= IRP_MJ_MAXIMUM_FUNCTION);
	return strings[majorFunction];
}
