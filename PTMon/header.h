#pragma once

#include <fltKernel.h>
#include "types.h"

#define DRIVER_TAG 'PTMO'
#define DRIVER_PREFIX "PTMON: "
#define MAX_ITEM_COUNT 1024

#define MAX_EXE_PATH_NUM 30
struct GlobalData {
	LIST_ENTRY linkHead;
	int itemCount;
	FastMutex mutex;
	WCHAR* exePath[MAX_EXE_PATH_NUM];
	USHORT exePathNum;
	FastMutex exePathMutex;
	LARGE_INTEGER regCookie;
};

template <typename T>
struct Item {
	LIST_ENTRY entry;
	T data;
};

void OnProcessNotify(PEPROCESS process, HANDLE processId, PPS_CREATE_NOTIFY_INFO info);
void OnThreadNotify(HANDLE processId, HANDLE threadId, BOOLEAN create);

void OnImageNotify(
    PUNICODE_STRING FullImageName,
    HANDLE ProcessId,                // pid into which image is being mapped
    PIMAGE_INFO ImageInfo
    );

NTSTATUS OnRegistryNotify(
	PVOID CallbackContext,
	PVOID argument1,
	PVOID argument2);

void PTMonUnload(PDRIVER_OBJECT driverObject);
NTSTATUS PTMonCreate(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS PTMonClose(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS PTMonRead(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS PTMonDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp);
bool IsWin8Below();

void PushItem(LIST_ENTRY* entry);
