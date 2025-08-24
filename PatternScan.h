#pragma once
#include <windows.h>
#include <vector>

inline uintptr_t PatternScan(HANDLE hProc, uintptr_t base, size_t size,
    const char* pattern, const char* mask) {
    std::vector<char> buffer(size);
    SIZE_T bytesRead;
    if (!ReadProcessMemory(hProc, (LPCVOID)base, buffer.data(), size, &bytesRead))
        return 0;

    for (size_t i = 0; i < size; i++) {
        bool found = true;
        for (size_t j = 0; pattern[j]; j++) {
            if (mask[j] == 'x' && pattern[j] != buffer[i + j]) {
                found = false;
                break;
            }
        }
        if (found) {
            return base + i;
        }
    }
    return 0;
}
