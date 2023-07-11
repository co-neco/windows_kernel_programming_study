#pragma once

#include <iostream>

void DisplayInfo(BYTE* buffer, int size);
void DisplayTime(LARGE_INTEGER time);

const std::string GetProcessNamePath(DWORD dwProcessId);