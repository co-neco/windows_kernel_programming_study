#include "common.h"
#include "header.h"

Globals g_globalData;

extern "C" 
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {

	auto status = STATUS_SUCCESS;

	g_globalData.Init();

	PDEVICE_OBJECT deviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\process_protector");
	bool bSymLinkCreated = false;


	do {
		UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\ProcessProtector");
		status = IoCreateDevice(driverObject, 0, &deviceName, FILE_DEVICE_UNKNOWN, 0, TRUE, &deviceObject);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create device 0x%08X\n", status));
			break;
		}

		deviceObject->Flags |= DO_DIRECT_IO;

		status = IoCreateSymbolicLink(&symLink, &deviceName);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to create symbolic link 0x%08X", status));
			break;
		}

		bSymLinkCreated = true;

		OB_OPERATION_REGISTRATION operations[] = {
			{
				PsProcessType,
				OB_OPERATION_HANDLE_CREATE | OB_OPERATION_HANDLE_DUPLICATE,
				PreOpenProcessCallback, nullptr
			}
		};

		OB_CALLBACK_REGISTRATION reg = {
			OB_FLT_REGISTRATION_VERSION,
			1,
			RTL_CONSTANT_STRING(L"12345.6789"),
			nullptr,
			operations
		};

		status = ObRegisterCallbacks(&reg, &g_globalData.regHandle);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register object callback"));
			break;
		}

	} while (false);

	if (!NT_SUCCESS(status)) {
		if (bSymLinkCreated)
			IoDeleteSymbolicLink(&symLink);

		if (deviceObject)
			IoDeleteDevice(deviceObject);
	}
	else {
		driverObject->DriverUnload = dispatch_routine::Unload;
		driverObject->MajorFunction[IRP_MJ_CREATE] = dispatch_routine::Create;
		driverObject->MajorFunction[IRP_MJ_CLOSE] = dispatch_routine::Close;
		driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = dispatch_routine::DeviceControl;
	}

	return status;
}

namespace dispatch_routine {

	void Unload(PDRIVER_OBJECT driverObject) {

		if (g_globalData.regHandle)
			ObUnRegisterCallbacks(g_globalData.regHandle);

		UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\process_protector");
		IoDeleteSymbolicLink(&symLink);
		IoDeleteDevice(driverObject->DeviceObject);
	}

	NTSTATUS Create(PDEVICE_OBJECT deviceObject, PIRP irp) {

		UNREFERENCED_PARAMETER(deviceObject);

		irp->IoStatus.Status = STATUS_SUCCESS;
		irp->IoStatus.Information = 0;

		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	NTSTATUS Close(PDEVICE_OBJECT deviceObject, PIRP irp) {

		UNREFERENCED_PARAMETER(deviceObject);

		irp->IoStatus.Status = STATUS_SUCCESS;
		irp->IoStatus.Information = 0;

		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return STATUS_SUCCESS;
	}

	NTSTATUS DeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {
		UNREFERENCED_PARAMETER(deviceObject);

		auto stack = IoGetCurrentIrpStackLocation(irp);
		auto status = STATUS_SUCCESS;
		auto len = 0;
		
		switch (stack->Parameters.DeviceIoControl.IoControlCode) {
		case IOCTL_PROCESS_PROTECT_BY_PID: {

			auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
			if (size % sizeof(ULONG) != 0) {
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			auto pid = (ULONG*)irp->AssociatedIrp.SystemBuffer;

			LockRAII<FastMutex> lock(g_globalData.mutex);

			for (int i = 0; i < size / sizeof(ULONG); ++i) {

				if (pid[i] == 0) {
					status = STATUS_INVALID_PARAMETER;
					break;
				}

				if (g_globalData.pidsCount == g_maxPids) {
					status = STATUS_TOO_MANY_CONTEXT_IDS;
					break;
				}

				auto index = FindProcessId(pid[i]);
				if (index != -1)
					continue;

				AddProcessId(pid[i]);
				len += sizeof(ULONG);
			}

			break;
		}
		case IOCTL_PROCESS_UNPROTECT_BY_PID: {

			auto size = stack->Parameters.DeviceIoControl.InputBufferLength;
			if (size % sizeof(ULONG) != 0) {
				status = STATUS_INVALID_BUFFER_SIZE;
				break;
			}

			auto pid = (ULONG*)irp->AssociatedIrp.SystemBuffer;

			LockRAII<FastMutex> lock(g_globalData.mutex);

			for (int i = 0; i < size / sizeof(ULONG); ++i) {

				if (g_globalData.pidsCount == 0)
					break;

				if (pid[i] == 0) {
					status = STATUS_INVALID_PARAMETER;
					break;
				}

				if (RemoveProcessId(pid[i]))
					len += sizeof(ULONG);
			}
			
			break;
		}
		case IOCTL_PROCESS_PROTECT_CLEAR: {
			
			LockRAII<FastMutex> lock(g_globalData.mutex);
			memset(g_globalData.pids, 0, sizeof(g_globalData.pids));
			g_globalData.pidsCount = 0;
			break;
		}
		default: {
			KdPrint((DRIVER_PREFIX "unknown device code"));
			status = STATUS_INVALID_DEVICE_REQUEST;
			break;
		}
		}

		irp->IoStatus.Status = status;
		irp->IoStatus.Information = len;

		IoCompleteRequest(irp, IO_NO_INCREMENT);
		return status;
	}
}

OB_PREOP_CALLBACK_STATUS PreOpenProcessCallback(PVOID, POB_PRE_OPERATION_INFORMATION info) {

	if (info->KernelHandle)
		return OB_PREOP_SUCCESS;

	auto process = info->Object;
	auto pid = HandleToUlong(PsGetProcessId((PEPROCESS)process));

	LockRAII<FastMutex> lock(g_globalData.mutex);

	auto index = FindProcessId(pid);
	if (index != -1)
		info->Parameters->CreateHandleInformation.DesiredAccess &= ~PROCESS_TERMINATE;

	return OB_PREOP_SUCCESS;
}

int FindProcessId(ULONG pid) {

	if (!g_globalData.pidsCount)
		return -1;

	for (int index = 0; index < g_maxPids; ++index) {
		auto curPid = g_globalData.pids[index];
		if (curPid && curPid == pid)
			return index;
	}

	return -1;
}

void AddProcessId(ULONG pid) {

	for (int index = 0; index < g_maxPids; ++index) {
		if (g_globalData.pids[index])
			continue;

		g_globalData.pids[index] = pid;
		++g_globalData.pidsCount;
		break;
	}
}

bool RemoveProcessId(ULONG pid) {

	auto idx = FindProcessId(pid);
	if (idx == -1)
		return false;

	g_globalData.pids[idx] = 0;
	--g_globalData.pidsCount;
	return true;
}
