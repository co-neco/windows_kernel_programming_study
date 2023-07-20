#pragma once

NTSTATUS GetProcessImageFileName(HANDLE hProcess, PCUNICODE_STRING* name);
NTSTATUS OpenProcessById(HANDLE processId, HANDLE* hProcess);
NTSTATUS GetParentProcessId(HANDLE processId, PHANDLE parentProcessId);
NTSTATUS GetProcessImageFileNameById(HANDLE processId, PCUNICODE_STRING* name);
NTSTATUS GetProcessImageFileNameByObject(PEPROCESS process, PCUNICODE_STRING* name);

bool IsDeleteAllowedByProcess(HANDLE hProcess);
