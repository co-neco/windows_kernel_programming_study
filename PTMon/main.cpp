#include "header.h"

#include "common.h"
#include "utils.h"

GlobalData g_globalData;

extern "C" 
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject, PUNICODE_STRING) {

	auto status = STATUS_SUCCESS;

	InitializeListHead(&g_globalData.linkHead);
	g_globalData.mutex.Init();
	g_globalData.exePathMutex.Init();
	RtlZeroMemory(&g_globalData.exePath, MAX_EXE_PATH_NUM * sizeof(WCHAR*));
	g_globalData.exePathNum = 0;

	PDEVICE_OBJECT deviceObject = nullptr;
	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ptmon");
	bool bSymLinkCreated = false;


	do {
		UNICODE_STRING deviceName = RTL_CONSTANT_STRING(L"\\Device\\ptmon");
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

		status = PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, FALSE);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register process callback 0x%08X", status));
			break;
		}

		status = PsSetCreateThreadNotifyRoutine(OnThreadNotify);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register thread callback 0x%08X", status));
			break;
		}

		status = PsSetLoadImageNotifyRoutine(OnImageNotify);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register image load callback 0x%08X", status));
			break;
		}

		UNICODE_STRING regAltitude = RTL_CONSTANT_STRING(L"7657.124");
		status = CmRegisterCallbackEx(OnRegistryNotify, &regAltitude, driverObject, NULL, &g_globalData.regCookie, NULL);
		if (!NT_SUCCESS(status)) {
			KdPrint((DRIVER_PREFIX "failed to register registry callback"));
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
		driverObject->DriverUnload = PTMonUnload;
		driverObject->MajorFunction[IRP_MJ_CREATE] = PTMonCreate;
		driverObject->MajorFunction[IRP_MJ_CLOSE] = PTMonClose;
		driverObject->MajorFunction[IRP_MJ_READ] = PTMonRead;
		driverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = PTMonDeviceControl;
	}

	return status;
}


void OnProcessNotify(PEPROCESS process, HANDLE processId, PPS_CREATE_NOTIFY_INFO info) {
	UNREFERENCED_PARAMETER(process);
	
	if (info) {
		USHORT allocSize = sizeof(Item<ProcessCreateInfo>);
		USHORT cmdLineSize = 0;
		USHORT imageFileNameSize = 0;
		PCUNICODE_STRING imageFileName = nullptr;

		if (info->CommandLine) {
			cmdLineSize = info->CommandLine->Length;
			allocSize += cmdLineSize;
		}

		if (info->ImageFileName) {
			imageFileName = info->ImageFileName;
			imageFileNameSize = info->ImageFileName->Length;
			allocSize += imageFileNameSize;
		}
		else {
			PCUNICODE_STRING name = nullptr;
			NTSTATUS status = GetProcessImageFileNameByObject(process, &name);
			if (NT_SUCCESS(status) && name) {
				imageFileNameSize = name->Length;
				allocSize += imageFileNameSize;
				imageFileName = name;
			}
		}

		auto item = (Item<ProcessCreateInfo>*)ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (!item) {
			KdPrint((DRIVER_PREFIX "failed paged pool's  allocation"));
			return;
		}

		auto& data = item->data;
		KeQuerySystemTime(&data.time);
		data.type = ItemType::ProcessCreate;
		data.size = sizeof(ProcessCreateInfo) + cmdLineSize + imageFileNameSize;
		data.processId = HandleToULong(processId);
		data.parentProcessId = HandleToULong(info->ParentProcessId);

		if (info->CommandLine) {

			memcpy((UCHAR*)&data + sizeof(data), info->CommandLine->Buffer, cmdLineSize);

		}
		else {
			data.cmdLineLength = 0;
		}

		if (imageFileName) {

			memcpy((UCHAR*)&data + sizeof(data) + cmdLineSize, imageFileName->Buffer, imageFileNameSize);

			data.imageFileNameLength = imageFileNameSize / sizeof(WCHAR);
			data.imageFileNameOffset = sizeof(data) + cmdLineSize;

		}
		else {
			data.imageFileNameLength = 0;
		}

		PushItem(&item->entry);

		// Check process path and abort its creation if needed
		LockRAII<FastMutex> lock(g_globalData.exePathMutex);
		if (imageFileName && FindExePathArrayIndex(imageFileName->Buffer))
			info->CreationStatus = STATUS_ACCESS_DENIED;

		// Free memory allocated when info->ImageFileName is nullptr
		if (imageFileName && !info->ImageFileName)
			ExFreePool((PVOID)imageFileName);
	}
	else {
		auto item = (Item<ProcessExitInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(Item<ProcessExitInfo>), DRIVER_TAG);
		if (!item) {
			KdPrint((DRIVER_PREFIX "failed paged pool's allocation"));
			return;
		}

		auto& data = item->data;
		KeQuerySystemTime(&data.time);
		data.type = ItemType::ProcessExit;
		data.size = sizeof(ProcessExitInfo);
		data.processId = HandleToULong(processId);

		PushItem(&item->entry);
	}
}

void OnThreadNotify(HANDLE processId, HANDLE threadId, BOOLEAN create) {

	if (create) {

		HANDLE remoteProcessId = NULL;
		bool remoteParentProcess = false;
		PCUNICODE_STRING remoteProcessName = nullptr;

		do {
			auto curProcessId = PsGetCurrentProcessId();
			if (curProcessId == processId)
				break;
			
			HANDLE parentProcessId;
			auto status = GetParentProcessId(processId, &parentProcessId);
			if (!NT_SUCCESS(status))
				break;

			if (parentProcessId == curProcessId)
				remoteParentProcess = true;

			remoteProcessId = curProcessId;

			GetProcessImageFileNameById(remoteProcessId, &remoteProcessName);
		} while (false);

		USHORT remoteProcessNameSize = 0;
		USHORT allocSize = sizeof(Item<ThreadCreateInfo>);
		if (remoteProcessName) {
			remoteProcessNameSize = remoteProcessName->Length;
			allocSize += remoteProcessNameSize;
		}
	
		auto item = (Item<ThreadCreateInfo>*)ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (!item) {
			KdPrint((DRIVER_PREFIX "failed paged pool's allocation"));
			return;
		}
		else
			RtlZeroMemory(item, allocSize);

		auto& data = item->data;

		KeQuerySystemTime(&data.time);
		data.type = ItemType::ThreadCreate;
		data.size = sizeof(ThreadCreateInfo) + remoteProcessNameSize;
		data.processId = HandleToULong(processId);
		data.threadId = HandleToULong(threadId);

		if (remoteProcessName) {
			memcpy((UCHAR*)&data + sizeof(ThreadCreateInfo), remoteProcessName->Buffer, remoteProcessNameSize);
			data.remoteProcessId = HandleToULong(remoteProcessId);
			data.remoteProcessNameLength = remoteProcessNameSize / sizeof(WCHAR);
			data.remoteProcessNameOffset = sizeof(ThreadCreateInfo);
			data.remoteParentProcess = remoteParentProcess;
			ExFreePool((PVOID)remoteProcessName);
		}
		else
			data.remoteProcessNameLength = 0;

		PushItem(&item->entry);
	}
	else {
		auto item = (Item<ThreadExitInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(Item<ThreadExitInfo>), DRIVER_TAG);
		if (!item) {
			KdPrint((DRIVER_PREFIX "failed paged pool's allocation"));
			return;
		}
		else
			RtlZeroMemory(item, sizeof(Item<ThreadExitInfo>));

		auto& data = item->data;

		KeQuerySystemTime(&data.time);
		data.type = ItemType::ThreadExit;
		data.size = sizeof(ThreadExitInfo);
		data.processId = HandleToULong(processId);
		data.threadId = HandleToULong(threadId);

		PushItem(&item->entry);
	}
}

void OnImageNotify(PUNICODE_STRING fullImageName, HANDLE processId, PIMAGE_INFO imageInfo) {

	if (!imageInfo) {
		KdPrint((DRIVER_PREFIX "empty imageInfo in image notify routine"));
		return;
	}

	USHORT imageFileNameLength = 0;
	if (fullImageName)
		imageFileNameLength = fullImageName->Length;

	auto item = (Item<ImageLoadInfo>*)ExAllocatePoolWithTag(PagedPool, sizeof(Item<ImageLoadInfo>) + imageFileNameLength, DRIVER_TAG);
	if (!item) {
		KdPrint((DRIVER_PREFIX "failed paged pool's allocation for image notify routine"));
		return;
	}

	auto& data = item->data;

	KeQuerySystemTime(&data.time);
	data.type = ItemType::ImageLoad;
	data.size = sizeof(ImageLoadInfo) + imageFileNameLength;
	data.processId = HandleToUlong(processId);

	data.imageBase = imageInfo->ImageBase;

	if (!fullImageName) {
		KdPrint((DRIVER_PREFIX "empty fullImageName"));
		data.imageFileNameLength = 0;
	}
	else {
		memcpy((UCHAR*)&data + sizeof(data), fullImageName->Buffer, imageFileNameLength);
		data.imageFileNameLength = fullImageName->Length / sizeof(WCHAR);
		data.imageFileNameOffset = sizeof(data);
	}

	PushItem(&item->entry);
}

NTSTATUS OnRegistryNotify(PVOID CallbackContext, PVOID argument1, PVOID argument2) {
	UNREFERENCED_PARAMETER(CallbackContext);
	
	switch ((REG_NOTIFY_CLASS)(ULONG_PTR)argument1) {
	case RegNtPostSetValueKey: {

		auto args = (REG_POST_OPERATION_INFORMATION*)argument2;
		if (!NT_SUCCESS(args->Status))
			break;

		static const WCHAR machine[] = L"\\REGISTRY\\MACHINE\\";

		PCUNICODE_STRING name;
		if (!NT_SUCCESS(CmCallbackGetKeyObjectID(&g_globalData.regCookie, args->Object, NULL, &name)))
			break;

		if (_wcsnicmp(name->Buffer, machine, ARRAYSIZE(machine) - 1) != 0)
			break;
		
		auto preInfo = (REG_SET_VALUE_KEY_INFORMATION*)args->PreInformation;
		NT_ASSERT(preInfo);

		auto allocSize = sizeof(Item<RegPostSetValueInfo>) + name->Length + preInfo->ValueName->Length + preInfo->DataSize;
		auto info = (Item<RegPostSetValueInfo>*)ExAllocatePoolWithTag(PagedPool, allocSize, DRIVER_TAG);
		if (!info) {
			KdPrint((DRIVER_PREFIX "Allocate paged pool failed"));
			break;
		}

		RtlZeroMemory(info, allocSize);
		auto& item = info->data;
		KeQuerySystemTime(&item.time);
		item.size = sizeof(item) + name->Length + preInfo->ValueName->Length + preInfo->DataSize;
		item.type = ItemType::RegPostSetValue;
		item.dataType = preInfo->Type;

		item.processId = HandleToUlong(PsGetCurrentProcessId());
		item.threadId = HandleToUlong(PsGetCurrentThreadId());
		
		memcpy((UCHAR*)&item + sizeof(item), name->Buffer, name->Length);
		item.keyNameLength = name->Length / sizeof(WCHAR);
		item.keyNameOffset = sizeof(item);

		memcpy((UCHAR*)&item + sizeof(item) + name->Length, preInfo->ValueName->Buffer, preInfo->ValueName->Length);
		item.valueNameLength = preInfo->ValueName->Length / sizeof(WCHAR);
		item.valueNameOffset = sizeof(item) + name->Length;

		memcpy((UCHAR*)&item + sizeof(item) + name->Length + preInfo->ValueName->Length, preInfo->Data, preInfo->DataSize);
		item.dataLength = preInfo->DataSize;
		item.dataOffset = sizeof(item) + name->Length + preInfo->ValueName->Length;

		PushItem(&info->entry);

		break;
	}
	}

	return STATUS_SUCCESS;
}

void PTMonUnload(PDRIVER_OBJECT driverObject) {

	while (g_globalData.itemCount > 0) {

		auto entry = RemoveHeadList(&g_globalData.linkHead);
		auto item = CONTAINING_RECORD(entry, Item<ItemHeader>, entry);
		ExFreePool(item);
		--g_globalData.itemCount;
	}

	PsSetCreateProcessNotifyRoutineEx(OnProcessNotify, TRUE);
	PsRemoveCreateThreadNotifyRoutine(OnThreadNotify);
	PsRemoveLoadImageNotifyRoutine(OnImageNotify);
	CmUnRegisterCallback(g_globalData.regCookie);

	UNICODE_STRING symLink = RTL_CONSTANT_STRING(L"\\??\\ptmon");
	IoDeleteSymbolicLink(&symLink);
	IoDeleteDevice(driverObject->DeviceObject);
}

NTSTATUS PTMonCreate(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS PTMonClose(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);

	irp->IoStatus.Status = STATUS_SUCCESS;
	irp->IoStatus.Information = 0;

	IoCompleteRequest(irp, IO_NO_INCREMENT);
	return STATUS_SUCCESS;
}

NTSTATUS PTMonRead(PDEVICE_OBJECT deviceObject, PIRP irp) {
	UNREFERENCED_PARAMETER(deviceObject);

	auto stack = IoGetCurrentIrpStackLocation(irp);
	auto len = stack->Parameters.Read.Length;
	auto status = STATUS_SUCCESS;
	int count = 0;

	NT_ASSERT(irp->MdlAddress);

	auto buffer = (UCHAR*)MmGetSystemAddressForMdlSafe(irp->MdlAddress, NormalPagePriority);
	if (buffer) {
		LockRAII<FastMutex> lock(g_globalData.mutex);

		while (g_globalData.itemCount > 0) {

			auto entry = RemoveHeadList(&g_globalData.linkHead);
			auto item = CONTAINING_RECORD(entry, Item<ItemHeader>, entry);
			auto size = item->data.size;

			if (len < size) {
				InsertHeadList(&g_globalData.linkHead, entry);
				break;
			}

			memcpy(buffer, &item->data, size);

			buffer += size;
			len -= size;
			count += size;
			ExFreePool(item);
			--g_globalData.itemCount;
		}
	}
	else {
		status = STATUS_INSUFFICIENT_RESOURCES;
	}
	
	irp->IoStatus.Status = status;
	irp->IoStatus.Information = count;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

NTSTATUS PTMonDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp) {

	UNREFERENCED_PARAMETER(deviceObject);

	auto stack = IoGetCurrentIrpStackLocation(irp);
	NTSTATUS status = STATUS_SUCCESS;

	switch (stack->Parameters.DeviceIoControl.IoControlCode) {
	case IOCTL_PTMON_ADD_EXECUTABLE: {
		
		auto name = (WCHAR*)irp->AssociatedIrp.SystemBuffer;
		if (!name) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		LockRAII<FastMutex> lock(g_globalData.exePathMutex);

		auto index = GetExePathArrayIndex(name);
		// Ignore saved executable path.
		if (index != (USHORT)-1)
			break;

		if (g_globalData.exePathNum == MAX_EXE_PATH_NUM) {
			status = STATUS_TOO_MANY_NAMES;
			break;
		}

		auto buffer = ExAllocatePoolWithTag(PagedPool, sizeof(WCHAR) * (wcslen(name) + 1), DRIVER_TAG);
		if (!buffer) {
			status = STATUS_INSUFFICIENT_RESOURCES;
			break;
		}

		memcpy(buffer, name, sizeof(WCHAR) * (wcslen(name) + 1));
		InsertExePath((WCHAR*)buffer);
		break;
	}
	case IOCTL_PTMON_REMOVE_EXECUTABLE: {

		auto name = (WCHAR*)irp->AssociatedIrp.SystemBuffer;
		if (!name) {
			status = STATUS_INVALID_PARAMETER;
			break;
		}

		LockRAII<FastMutex> lock(g_globalData.exePathMutex);
		
		auto index = GetExePathArrayIndex(name);
		if (index == (USHORT)-1) {
			status = STATUS_NOT_FOUND;
			break;
		}

		RemoveExePath(index);
		break;
	}
	case IOCTL_PTMON_CLEAR: {

		LockRAII<FastMutex> lock(g_globalData.exePathMutex);
		ClearExePath();
		break;
	}
	default: {
		KdPrint((DRIVER_PREFIX "invalid io control code"));
		status = STATUS_INVALID_DEVICE_REQUEST;
		break;
	}
	}

	irp->IoStatus.Status = status;
	irp->IoStatus.Information = 0;
	IoCompleteRequest(irp, IO_NO_INCREMENT);

	return status;
}

void PushItem(LIST_ENTRY* entry) {

	LockRAII<FastMutex> lock(g_globalData.mutex);

	if (g_globalData.itemCount > MAX_ITEM_COUNT) {

		auto head = RemoveHeadList(&g_globalData.linkHead);
		--g_globalData.itemCount;
		auto item = CONTAINING_RECORD(head, Item<ItemHeader>, entry);
		ExFreePool(item);
	}

	InsertTailList(&g_globalData.linkHead, entry);
	++g_globalData.itemCount;
}

bool IsWin8Below() {

	RTL_OSVERSIONINFOEXW version = { sizeof(RTL_OSVERSIONINFOEXW) };
	NTSTATUS status = RtlGetVersion((PRTL_OSVERSIONINFOW)&version);
	if (!NT_SUCCESS(status)) {
		KdPrint((DRIVER_PREFIX "RtlGetVersion failed"));
		return true;
	}

	if (version.dwMajorVersion < 6
		||
		(version.dwMajorVersion == 6
			&& (version.dwMinorVersion < 2
				|| (version.dwMinorVersion == 2 && version.wProductType != VER_NT_WORKSTATION))))
		return true;
	else
		return false;
}
