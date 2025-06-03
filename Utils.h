#pragma once
#include <string>
#include <vector>
#include <windows.h>

std::string GetLastErrorMessage(const DWORD err);
std::vector<wchar_t> StringToWChar(const std::string& str);
