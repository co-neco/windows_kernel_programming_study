// FilterProcessCreation.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <Windows.h>
#include <iostream>
#include <string>

#include "../PTMon/common.h"

int main(int argc, char** argv) {

	int result = 0;

	auto hFile = CreateFileW(L"\\\\.\\ptmon", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		std::cout << "Can not open PTMon" << std::endl;
		return -1;
	}

	int option = std::stoi(argv[1]);
	if (option == 0) {
		const WCHAR* calc = L"calc.exe";
		DWORD returnedLength = 0;
		auto bOk = DeviceIoControl(hFile, IOCTL_PTMON_ADD_EXECUTABLE, (LPVOID)calc, (DWORD)(wcslen(calc) * sizeof(WCHAR)), NULL, 0, &returnedLength, NULL);
		if (!bOk) {
			std::cout << "Add executable failed" << std::endl;
			result = -1;
		}
	}
	else if (option == 1) {
		const WCHAR* calc = L"calc.exe";
		DWORD returnedLength = 0;
		auto bOk = DeviceIoControl(hFile, IOCTL_PTMON_REMOVE_EXECUTABLE, (LPVOID)calc, (DWORD)(wcslen(calc) * sizeof(WCHAR)), NULL, 0, &returnedLength, NULL);
		if (!bOk) {
			std::cout << "Remove executable failed" << std::endl;
			result = -1;
		}
	}
	else if (option == 2) {
		DWORD returnedLength = 0;
		auto bOk = DeviceIoControl(hFile, IOCTL_PTMON_CLEAR, NULL, 0, NULL, 0, &returnedLength, NULL);
		if (!bOk) {
			std::cout << "Clear executable failed" << std::endl;
			result = -1;
		}
	}

	CloseHandle(hFile);
	return result;
}

// Run program: Ctrl + F5 or Debug > Start Without Debugging menu
// Debug program: F5 or Debug > Start Debugging menu

// Tips for Getting Started: 
//   1. Use the Solution Explorer window to add/manage files
//   2. Use the Team Explorer window to connect to source control
//   3. Use the Output window to see build output and other messages
//   4. Use the Error List window to view errors
//   5. Go to Project > Add New Item to create new code files, or Project > Add Existing Item to add existing code files to the project
//   6. In the future, to open this project again, go to File > Open > Project and select the .sln file
