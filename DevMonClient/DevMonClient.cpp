
#include <windows.h>
#include <iostream>
#include <string>

#include "../DevMon/common.h"

int wmain(int argc, const wchar_t* argv[]) {

	if (argc != 3) {
		std::cout << "Insufficient parameters\n";
		return 0;
	}

	enum class Options {
		Unknown, Add, Remove, Clear };

	Options option;
	std::wstring arg1(argv[1]);
	if (arg1 == L"add")
		option = Options::Add;
	else if (arg1 == L"remove")
		option = Options::Remove;
	else if (arg1 == L"clear")
		option = Options::Clear;
	else {
		std::wcout << L"Unknwon options: " << arg1 << L"\n";
		return -1;
	}

	HANDLE hFile = CreateFileW(L"\\\\.\\KDevMon", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		std::cout << "Can not open delection protector\n";
		return -1;
	}

	std::wstring devName(argv[2]);
	std::wcout << L"devName: " << devName << L"\n";

	BOOL bOk = TRUE;
	DWORD dwBytesReturned;

	switch (option) {
	case Options::Add: {
		bOk = DeviceIoControl(hFile, IOCTL_DEVMON_ADD_DEVICE, (LPVOID)devName.c_str(), (DWORD)((devName.length() + 1) * sizeof(WCHAR)), NULL, 0, &dwBytesReturned, NULL);
		break;
		}
	case Options::Remove: {
		bOk = DeviceIoControl(hFile, IOCTL_DEVMON_REMOVE_DEVICE, (LPVOID)devName.c_str(), (DWORD)((devName.length() + 1) * sizeof(WCHAR)), NULL, 0, &dwBytesReturned, NULL);
		break;
	}
	case Options::Clear: {
		bOk = DeviceIoControl(hFile, IOCTL_DEVMON_REMOVE_ALL_DEVICE, NULL, 0, NULL, 0, &dwBytesReturned, NULL);
		break;
	}
	}

	if (!bOk)
		std::cout << "DeviceIoControl failed\n";
	else
		std::cout << "Operation succeed\n";

	CloseHandle(hFile);
	return 0;
}
