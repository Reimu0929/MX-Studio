#pragma once

#include <windows.h>
#include <string>

HWND createLevelMeter(HWND parent, HINSTANCE instance, int controlId);
void setLevelMeter(HWND meter, double dbfs, bool clip, const std::wstring& label, bool active);
