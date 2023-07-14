
#include <windows.h>
#include <iostream>
#include <vector>

#include "../ProcessProtector/common.h"

int main(int argc, const char* argv[]) {

	if (argc < 2) {
		std::cout << "Insufficient parameters\n";
		return 0;
	}

	enum class Options {
		Unknown, Add, Remove, Clear };

	Options option;
	std::string arg1(argv[1]);
	if (arg1 == "add")
		option = Options::Add;
	else if (arg1 == "remove")
		option = Options::Remove;
	else if (arg1 == "clear")
		option = Options::Clear;
	else {
		std::cout << "Unknwon options: " << arg1 << "\n";
		return -1;
	}

	HANDLE hFile = CreateFileW(L"\\\\.\\process_protector", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		std::cout << "Can not open process protector\n";
		return -1;
	}

	std::vector<DWORD> pids;
	for (int idx = 2; idx < argc; ++idx) {
		pids.push_back(std::atoi(argv[idx]));
	}

	BOOL bOk = TRUE;
	DWORD dwBytesReturned;

	switch (option) {
	case Options::Add: {
		bOk = DeviceIoControl(hFile, IOCTL_PROCESS_PROTECT_BY_PID, pids.data(), (DWORD)(pids.size() * sizeof(DWORD)), NULL, 0, &dwBytesReturned, NULL);
		break;
		}
	case Options::Remove: {
		bOk = DeviceIoControl(hFile, IOCTL_PROCESS_UNPROTECT_BY_PID, pids.data(), (DWORD)(pids.size() * sizeof(DWORD)), NULL, 0, &dwBytesReturned, NULL);
		break;
	}
	case Options::Clear: {
		bOk = DeviceIoControl(hFile, IOCTL_PROCESS_PROTECT_CLEAR, NULL, 0, NULL, 0, &dwBytesReturned, NULL);
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
