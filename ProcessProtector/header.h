#pragma once

#include <ntddk.h>

#define DRIVER_PREFIX "ProcessProtector: "

#define PROCESS_TERMINATE 1

#include "../PTMon/types.h"

const int g_maxPids = 256;

struct Globals {
	int pidsCount;
	ULONG pids[g_maxPids];
	FastMutex mutex;
	PVOID regHandle;

	void Init() {
		mutex.Init();
		regHandle = NULL;
	}
};

OB_PREOP_CALLBACK_STATUS PreOpenProcessCallback(
	PVOID ,
	POB_PRE_OPERATION_INFORMATION info 
);

namespace dispatch_routine {

void Unload(PDRIVER_OBJECT driverObject);
NTSTATUS Create(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS Close(PDEVICE_OBJECT deviceObject, PIRP irp);
NTSTATUS DeviceControl(PDEVICE_OBJECT deviceObject, PIRP irp);
}

int FindProcessId(ULONG pid);
void AddProcessId(ULONG pid);
bool RemoveProcessId(ULONG pid);
