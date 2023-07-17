#pragma once

#include <iostream>

void DisplayInfo(BYTE* buffer, int size);
void DisplayTime(LARGE_INTEGER time);
void DisplayBinary(BYTE* data, ULONG len);


const std::string GetProcessNamePath(DWORD dwProcessId);