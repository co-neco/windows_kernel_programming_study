#include "header.h"

#include "utils.h"

extern GlobalData g_globalData;

NTSTATUS GetProcessImageFileName(HANDLE hProcess, PCUNICODE_STRING* name) {

	NTSTATUS status = STATUS_SUCCESS;

	ULONG returnedLength;
	status = ZwQueryInformationProcess(hProcess, ProcessImageFileName, NULL, 0, &returnedLength);
	if (status != STATUS_INFO_LENGTH_MISMATCH)
		return status;

	PVOID buffer = (WCHAR*)ExAllocatePoolWithTag(PagedPool, returnedLength, DRIVER_TAG);
	if (!buffer) {
		status = STATUS_INSUFFICIENT_RESOURCES;
		return status;
	}

	status = ZwQueryInformationProcess(hProcess, ProcessImageFileName, buffer, returnedLength, &returnedLength);
	if (!NT_SUCCESS(status))
		return status;

	*name = (PCUNICODE_STRING)buffer;
	
	return status;
}

NTSTATUS OpenProcessById(HANDLE processId, HANDLE* hProcess) {

	CLIENT_ID clientId = { processId, NULL };
	OBJECT_ATTRIBUTES attri;
	NTSTATUS status = STATUS_SUCCESS;

	InitializeObjectAttributes(&attri, nullptr, 0, nullptr, nullptr);

	status = ZwOpenProcess(hProcess, GENERIC_READ, &attri, &clientId);
	if (!NT_SUCCESS(status))
		return status;

	return status;
}

NTSTATUS GetProcessImageFileNameById(HANDLE processId, PCUNICODE_STRING* name) {

	HANDLE hProcess;
	NTSTATUS status = OpenProcessById(processId, &hProcess);
	if (!NT_SUCCESS(status))
		return status;

	status = GetProcessImageFileName(hProcess, name);

	ZwClose(hProcess);
	return status;
}

NTSTATUS GetProcessImageFileNameByObject(PEPROCESS process, PCUNICODE_STRING* name) {
	
	HANDLE hProcess;
	NTSTATUS status = ObOpenObjectByPointer(process, OBJ_KERNEL_HANDLE, NULL, 0, *PsProcessType, KernelMode, &hProcess);
	if (!NT_SUCCESS(status))
		return status;

	status = GetProcessImageFileName(hProcess, name);

	ZwClose(hProcess);
	return status;
}

USHORT GetExePathArrayIndex(WCHAR* name) {

	for (USHORT index = 0; index < MAX_EXE_PATH_NUM; ++index) {
		if (!g_globalData.exePath[index])
			continue;

		if (_wcsicmp(g_globalData.exePath[index], name) == 0)
			return index;
	}

	return (USHORT)-1;
}

bool FindExePathArrayIndex(WCHAR* name) {

	for (USHORT index = 0; index < MAX_EXE_PATH_NUM; ++index) {
		if (!g_globalData.exePath[index])
			continue;

		if (wcsstr(name, g_globalData.exePath[index]) != 0)
			return TRUE;
	}

	return FALSE;
}

NTSTATUS GetParentProcessId(HANDLE processId, PHANDLE parentProcessId) {

	HANDLE hProcess;
	NTSTATUS status = OpenProcessById(processId, &hProcess);
	if (!NT_SUCCESS(status))
		return status;

	PROCESS_BASIC_INFORMATION info;
	status = ZwQueryInformationProcess(hProcess, ProcessBasicInformation, &info, sizeof(info), NULL);

	ZwClose(hProcess);

	if (!NT_SUCCESS(status))
		return status;

	*parentProcessId = (HANDLE)info.InheritedFromUniqueProcessId;
	return status;
}


void InsertExePath(WCHAR* name) {

	for (USHORT index = 0; index < MAX_EXE_PATH_NUM; ++index) {
		if (!g_globalData.exePath[index]) {
			g_globalData.exePath[index] = name;
			++g_globalData.exePathNum;
			break;
		}
	}
}

void RemoveExePath(USHORT index) {
	
	auto buffer = g_globalData.exePath[index];
	NT_ASSERT(buffer);

	ExFreePool(buffer);
	g_globalData.exePath[index] = nullptr;
	--g_globalData.exePathNum;
}

void ClearExePath() {

	for (USHORT index = 0; index < MAX_EXE_PATH_NUM; ++index) {
		if (!g_globalData.exePath[index])
			continue;

		ExFreePool(g_globalData.exePath[index]);
		g_globalData.exePath[index] = nullptr;
	}

	g_globalData.exePathNum = 0;
}
