#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
#include <iostream>

#include "GameState.h"
#include "Vec3.h"

// quick sanity checks
static inline bool is_user_canonical(uint64_t p) {
    return p >= 0x00000000010000ULL && p <= 0x00007FFFFFFFFFFFULL;
}
static inline bool printable_ascii(const std::string& s) {
    if (s.empty()) return false;
    for (unsigned char c : s) if (c < 32 || c > 126) return false;
    return true;
}

class MemoryReader {
public:
    MemoryReader(HANDLE procHandle, uintptr_t moduleBase, size_t moduleSize)
        : hProc_(procHandle), base_(moduleBase), moduleSize_(moduleSize) {
    }

    template <typename T>
    T read(uintptr_t addr) const {
        T val{};
        SIZE_T n = 0;
        ReadProcessMemory(hProc_, (LPCVOID)addr, &val, sizeof(T), &n);
        return val;
    }
    bool readBytes(uintptr_t addr, void* out, size_t len) const {
        SIZE_T n = 0;
        return !!ReadProcessMemory(hProc_, (LPCVOID)addr, out, len, &n);
    }
    Vec3 readVec3(uintptr_t addr) const {
        struct { float x, y, z; } raw{};
        readBytes(addr, &raw, sizeof(raw));
        return Vec3(raw.x, raw.y, raw.z);
    }

    // ---- Names
    std::string getNameFromId(uintptr_t gnamesAbs, int id) const;

    // ---- UWorld
    uintptr_t findUWorldPattern() const;
    uintptr_t findUWorldByObjects(uintptr_t gObjectsAbs, uintptr_t gnamesAbs, int scanLimit = 1200000) const;

    // ---- Actor array
    bool findActorArray(uintptr_t uWorld,
        uintptr_t gnamesAbs,
        uintptr_t& outPersistentLevel,
        uintptr_t& outActorArrayDataPtr,
        int& outActorCount) const;
    int  peekArrayCount(uintptr_t persistentLevel, uintptr_t actorArrayDataPtr) const;

    // ---- Actor search
    uintptr_t findActor(const std::string& targetName,
        uintptr_t actorArrayDataPtr,
        uintptr_t gnamesAbs,
        int actorCount,
        int debugN = 0) const;

    uintptr_t findActorViaObjects(const std::string& targetName,
        uintptr_t gObjectsAbs,
        uintptr_t gnamesAbs,
        int scanLimit = 1200000,
        int debugEvery = 0) const;

    // ---- Entity readers
    BallState getBall(uintptr_t ballAddr) const;
    CarState  getCar(uintptr_t carAddr)  const;

private:
    // module pattern scan
    uintptr_t patternScan(const char* pattern, const char* mask) const;

    // TArray resolvers (work for both Actor arrays and GObjects/GNames variants)
    bool resolveTArrayAt(uintptr_t arrayAddr, uintptr_t& data, int& num) const;
    bool resolveGObjects(uintptr_t gObjectsAbs, uintptr_t& data, int& num) const;

    // UE3-style GNames chunk decode attempt
    std::string tryUE3Name(uintptr_t gnamesAbs, int id) const;

private:
    HANDLE    hProc_{};
    uintptr_t base_{};
    size_t    moduleSize_{};
};
