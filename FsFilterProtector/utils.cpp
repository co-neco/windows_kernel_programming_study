#include <fltKernel.h>

#include "common.h"
#include "types.h"
#include "utils.h"

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

bool IsDeleteAllowedByProcess(HANDLE hProcess) {

	bool bOk = true;

    PCUNICODE_STRING name;
    NTSTATUS status = GetProcessImageFileName(hProcess, &name);
    if (!NT_SUCCESS(status)) {
        KdPrint((DRIVER_PREFIX "failed to get process image file name"));
        return bOk;
    }

    if (wcsstr(name->Buffer, L"\\System32\\cmd.exe") != nullptr
        ||
        wcsstr(name->Buffer, L"SysWOW64\\cmd.exe") != nullptr)
		bOk = false;

	ExFreePool((PVOID)name);
	return bOk;
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
