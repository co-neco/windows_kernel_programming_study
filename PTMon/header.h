#pragma once

#include <fltKernel.h>
#include "types.h"

#define DRIVER_TAG 'PTMO'
#define DRIVER_PREFIX "PTMON: "
#define MAX_ITEM_COUNT 1024

class FastMutex {
public:
	inline void Init() { ExInitializeFastMutex(&_mutex); }

	inline void Lock() { ExAcquireFastMutex(&_mutex); }
	inline void Unlock() { ExReleaseFastMutex(&_mutex); }

private:
	FAST_MUTEX _mutex;
};

template <typename T>
class LockRAII {
public:
	inline LockRAII(T& lock): _lock(lock) { _lock.Lock(); }
	inline ~LockRAII() { _lock.Unlock(); }

private:
	T& _lock;
};

#define MAX_EXE_PATH_NUM 30
struct GlobalData {
	LIST_ENTRY linkHead;
	int itemCount;
	FastMutex mutex;
	WCHAR* exePath[MAX_EXE_PATH_NUM];
	USHORT exePathNum;
	FastMutex exePathMutex;
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

void PTMonUnload(PDRIVER_OBJECT driverObject);
NTSTATUS PTMonCreate(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS PTMonClose(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS PTMonRead(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS PTMonDeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp);
bool IsWin8Below();

void PushItem(LIST_ENTRY* entry);
