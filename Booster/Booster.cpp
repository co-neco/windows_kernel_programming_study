#include <windows.h>
#include <stdio.h>
#include "..\PriorityBooster\PriorityBoosterCommon.h"

int Error(const char* message);

int main(int argc, const char* argv[]) {
	if (argc < 3) {
		printf("Usage: Booster <threadid> <priority>\n");
		return 0;
	}

	HANDLE hDevice = CreateFileW(L"\\\\.\\PriorityBooster", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
	if (hDevice == INVALID_HANDLE_VALUE)
		return Error("Failed to open device");

	ThreadData data;
	data.threadId = atoi(argv[1]);
	data.priority = atoi(argv[2]);

	DWORD returned;
	BOOL success = DeviceIoControl(hDevice, IOCTL_PRIORITY_BOOSTER_SET_PRIORITY,
		&data, sizeof(data), nullptr, 0, &returned, nullptr);
	if (success)
		printf("Priority change succeeded!\n");
	else
		Error("Priority change failed!");

	CloseHandle(hDevice);
	return 0;
}

int Error(const char* message) {
	printf("%s (error=%d)\n", message, GetLastError());
	return 1;
}
