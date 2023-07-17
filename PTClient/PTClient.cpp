// PTClient.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <cassert>
#include <windows.h>

#include "header.h"
#include "../PTMon/common.h"

#include <Psapi.h>
#pragma comment(lib, "Psapi.lib")

int main() {

	auto hFile = CreateFileW(L"\\\\.\\ptmon", GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, nullptr);
	if (hFile == INVALID_HANDLE_VALUE) {
		std::cout << "Can not open PTMon" << std::endl;
		return -1;
	}

	BYTE* buffer = new BYTE[1 << 16];

	while (true) {
		
		DWORD bytesRead;

		BOOL bOk = ReadFile(hFile, buffer, 1 << 16, &bytesRead, NULL);
		if (!bOk) {
			std::cout << "ReadFile failed with error code: " << GetLastError() << std::endl;
			break;
		}

		if (bytesRead == 0)
			continue;

		DisplayInfo(buffer, bytesRead);

		Sleep(200);
	}
}

void DisplayInfo(BYTE* buffer, int size) {

	int count = size;

	while (count > 0) {
		
		auto data = (ItemHeader*)buffer;
		switch (data->type) {

			case ItemType::ProcessCreate: {
				
				ProcessCreateInfo* pcInfo = static_cast<ProcessCreateInfo*>(data);

				DisplayTime(data->time);
				if (pcInfo->cmdLineLength != 0) {
					std::wstring cmdLine((const wchar_t*)(buffer + pcInfo->cmdLineOffset), pcInfo->cmdLineLength);
					printf("Process %d Created. Command line: %ws\n", pcInfo->processId, cmdLine.c_str());
				}

				if (pcInfo->imageFileNameLength != 0) {
					std::wstring imageFileName((const wchar_t*)(buffer + pcInfo->imageFileNameOffset), pcInfo->imageFileNameLength);
					printf("Image file name is %ws\n", imageFileName.c_str());
				}
				break;
			}
			case ItemType::ProcessExit: {

				auto peInfo = (ProcessExitInfo*)buffer;

				DisplayTime(data->time);
				printf("Process %d exited\n", peInfo->processId);
				break;
			}
			case ItemType::ThreadCreate: {

				auto tcInfo = (ThreadCreateInfo*)buffer;
				
				DisplayTime(data->time);

				printf("Thread %d ", tcInfo->threadId);
				if (data->type == ItemType::ThreadCreate)
					printf("created ");
				else
					printf("exited ");
				printf("in process %d", tcInfo->processId);

				if (tcInfo->remoteProcessNameLength != 0) {

					printf(", ");
					std::wstring remoteProcessName((const wchar_t*)(buffer + tcInfo->remoteProcessNameOffset), tcInfo->remoteProcessNameLength);
					printf("(remote ");
					if (tcInfo->remoteParentProcess)
						printf("parent ");
					printf("process[id:% d]: % ws)", tcInfo->remoteProcessId, remoteProcessName.c_str());
				}
				else
					printf("\n");

				break;
			}
			case ItemType::ThreadExit: {

				auto teInfo = (ThreadExitInfo*)buffer;

				DisplayTime(data->time);

				printf("Thread %d ", teInfo->threadId);
				if (data->type == ItemType::ThreadCreate)
					printf("created ");
				else
					printf("exited ");
				printf("in process %d\n", teInfo->processId);

				break;
			}
			case ItemType::ImageLoad: {

			/*	auto imageInfo = (ImageLoadInfo*)buffer;
				DisplayTime(imageInfo->time);

				printf("ImageLoad in process %d. ", imageInfo->processId);
				printf("Image base %p ", imageInfo->imageBase);
				if (imageInfo->imageFileNameLength == 0)
					printf(".\n");
				else {
					std::wstring imageFullPath((const wchar_t*)(buffer + imageInfo->imageFileNameOffset), imageInfo->imageFileNameLength);
					printf("with full path: %ws\n", imageFullPath.c_str());
				}*/
				break;
			}
			case ItemType::RegPostSetValue: {

				DisplayTime(data->time);

				auto info = (RegPostSetValueInfo*)buffer;
				std::wstring keyName((const wchar_t*)(buffer + info->keyNameOffset), info->keyNameLength);
				std::wstring valueName((const wchar_t*)(buffer + info->valueNameOffset), info->valueNameLength);
				printf("Registry write pid=%d, tid=%d, %ws\\%ws, type: %d, size: %d, data: ",
					info->processId, info->threadId,
					keyName.c_str(), valueName.c_str(),
					info->dataType, info->dataLength);

				void* data = buffer + info->dataOffset;
				switch (info->dataType) {
					case REG_DWORD: {
						printf("0x%08X", *(DWORD*)data);
						break;
					}
					case REG_SZ:
					case REG_EXPAND_SZ: {
						std::wstring dataStr((const wchar_t*)data, info->dataLength / sizeof(WCHAR));
						printf("%ws", dataStr.c_str());
						break;
					}
					default: {
						DisplayBinary((BYTE*)data, info->dataLength);
						break;
					}
				}
				printf("\n");
				break;
			}
			default: {
				std::cout << "Unknown type: " << static_cast<int>(data->type) << std::endl;
				assert(false);
				return;
			}
		}

		count -= data->size;
		buffer += data->size;
	}

}

void DisplayTime(LARGE_INTEGER time) {

	SYSTEMTIME st;

	FileTimeToSystemTime((FILETIME*)&time, &st);
	printf("%02d:%02d:%02d:%03d: ", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds);
}

void DisplayBinary(BYTE* data, ULONG len) {

	for (ULONG i = 0; i < len; ++i)
		printf(" 0x%02X", data[i]);
}

const std::string GetProcessNamePath(DWORD dwProcessId)
{
	HANDLE hProcess = NULL;
	CHAR szFileName[MAX_PATH] = { 0 };
	std::string szProcessName;

	hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, dwProcessId);
	if (hProcess == NULL)
		return szProcessName;

	// Do not use GetModuleFileName, because it would fail on previous windows version.
	// And its root problem is the failure of ReadProcessMemory
	if (GetProcessImageFileNameA(hProcess, szFileName, _countof(szFileName)))
		szProcessName = szFileName;

	CloseHandle(hProcess);

	//NormalizeNTPath(szProcessName);
	return szProcessName;
}