
#include <windows.h>
#include <iostream>
#include <vector>

#include "../FsFilterProtector/common.h"

int wmain(int argc, const wchar_t* argv[]) {

	if (argc < 2) {
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

	HANDLE hFile = CreateFileW(L"\\\\.\\del_protect", GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		std::cout << "Can not open delection protector\n";
		return -1;
	}

	std::vector<std::wstring> dirs;
	for (int idx = 2; idx < argc; ++idx) {
		dirs.push_back(argv[idx]);
	}

	BOOL bOk = TRUE;
	DWORD dwBytesReturned;

	switch (option) {
	case Options::Add: {
		bOk = DeviceIoControl(hFile, IOCTL_DELPROTECT_ADD_DIR, (LPVOID)(dirs[0].c_str()), (DWORD)((dirs[0].size() + 1) * sizeof(WCHAR)), NULL, 0, &dwBytesReturned, NULL);
		break;
		}
	case Options::Remove: {
		bOk = DeviceIoControl(hFile, IOCTL_DELPROTECT_REMOVE_DIR, (LPVOID)(dirs[0].c_str()), (DWORD)((dirs[0].size() + 1) * sizeof(WCHAR)), NULL, 0, &dwBytesReturned, NULL);
		break;
	}
	case Options::Clear: {
		bOk = DeviceIoControl(hFile, IOCTL_DELPROTECT_CLEAR_DIR, NULL, 0, NULL, 0, &dwBytesReturned, NULL);
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
