#pragma once
#include <Windows.h>

template <typename T>
bool WriteMemory(HANDLE hProc, uintptr_t address, const T& value) {
    return WriteProcessMemory(hProc, (LPVOID)address, &value, sizeof(T), nullptr);
}
