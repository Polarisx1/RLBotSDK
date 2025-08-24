#pragma once
#include <windows.h>
#include <string>
#include <iostream>
#include <vector>
#include <cmath>
#define _USE_MATH_DEFINES

#include "GameState.h"
#include "Vec3.h"

// ------------------------------------------
// Pattern scanner helper
// ------------------------------------------
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

class MemoryReader {
private:
    HANDLE hProc;

public:
    MemoryReader(HANDLE procHandle) : hProc(procHandle) {}

    template <typename T>
    T read(uintptr_t addr) {
        T val{};
        ReadProcessMemory(hProc, (LPCVOID)addr, &val, sizeof(T), nullptr);
        return val;
    }

    bool readBytes(uintptr_t addr, void* buffer, SIZE_T size) {
        SIZE_T bytesRead;
        return ReadProcessMemory(hProc, (LPCVOID)addr, buffer, size, &bytesRead) && bytesRead == size;
    }

    Vec3 readVec3(uintptr_t addr) {
        struct { float x, y, z; } raw{};
        ReadProcessMemory(hProc, (LPCVOID)addr, &raw, sizeof(raw), nullptr);
        return Vec3(raw.x, raw.y, raw.z);
    }

    // ---------------------------
    // Ball and Car state readers
    // ---------------------------
    BallState getBall(uintptr_t ballAddr) {
        BallState ball{};
        if (!ballAddr) return ball;

        ball.position = readVec3(ballAddr + 0x11C);
        ball.velocity = readVec3(ballAddr + 0x124);
        return ball;
    }

    CarState getCar(uintptr_t carAddr) {
        CarState car{};
        if (!carAddr) return car;

        car.position = readVec3(carAddr + 0x11C);
        car.velocity = readVec3(carAddr + 0x124);

        car.rotation.pitch = read<float>(carAddr + 0x148);
        car.rotation.yaw = read<float>(carAddr + 0x14C);
        car.rotation.roll = read<float>(carAddr + 0x150);

        car.boost = read<float>(carAddr + 0xAB0);
        car.isDemolished = read<bool>(carAddr + 0x7F0);
        return car;
    }

    // ---------------------------
    // GNames lookup
    // ---------------------------
    std::string getNameFromId(uintptr_t gNames, int id) {
        if (id < 0) return "INVALID";

        uintptr_t chunk = read<uintptr_t>(gNames + (id / 0x4000) * sizeof(uintptr_t));
        if (!chunk) return "NULL_CHUNK";

        uintptr_t entry = chunk + (id % 0x4000) * sizeof(uintptr_t);
        char buffer[64]{};
        if (!ReadProcessMemory(hProc, (LPCVOID)entry, buffer, sizeof(buffer), nullptr))
            return "READ_FAIL";

        return std::string(buffer);
    }

    // ---------------------------
    // Find UWorld with pattern scan
    // ---------------------------
    uintptr_t findUWorldPattern(uintptr_t base, size_t size) {
        const char pattern[] = "\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74\x3B";
        const char mask[] = "xxx????xxxxx";

        uintptr_t addr = PatternScan(hProc, base, size, pattern, mask);
        if (!addr) return 0;

        int32_t relOffset;
        ReadProcessMemory(hProc, (LPCVOID)(addr + 3), &relOffset, sizeof(relOffset), nullptr);
        uintptr_t uWorldAddr = addr + 7 + relOffset;

        std::cout << "[+] UWorld (pattern) at 0x" << std::hex << uWorldAddr << std::dec << "\n";
        return uWorldAddr;
    }

    // ---------------------------
    // Fallback: UWorld by GObjects scan
    // ---------------------------
    uintptr_t findUWorld(uintptr_t gObjects, uintptr_t gNames) {
        const int maxObjects = 300000;
        for (int i = 0; i < maxObjects; i++) {
            uintptr_t object = read<uintptr_t>(gObjects + i * sizeof(uintptr_t));
            if (!object) continue;

            int nameId = read<int>(object + 0x18);
            std::string name = getNameFromId(gNames, nameId);

            if (name.find("World") != std::string::npos) {
                std::cout << "[+] UWorld (GObjects) at 0x" << std::hex << object << std::dec << "\n";
                return object;
            }
        }
        return 0;
    }

    // ---------------------------
    // Debug scan for ActorArray
    // ---------------------------
    uintptr_t debugFindActorArray(uintptr_t persistentLevel) {
        std::cout << "[DBG] Scanning PersistentLevel (0x"
            << std::hex << persistentLevel << std::dec << ") for actor arrays...\n";

        for (int offset = 0x80; offset < 0x400; offset += 0x8) {
            uintptr_t candidateArray = read<uintptr_t>(persistentLevel + offset);
            int count = read<int>(persistentLevel + offset + 0x8);
            int max = read<int>(persistentLevel + offset + 0xC);

            if (candidateArray && count > 0 && count < 20000 && max >= count && max < 25000) {
                std::cout << "[+] ActorArray guess at offset 0x" << std::hex << offset
                    << " -> ptr=0x" << candidateArray
                    << " count=" << std::dec << count
                    << " max=" << max << "\n";
                return candidateArray;
            }
        }
        std::cout << "[!] No valid ActorArray found.\n";
        return 0;
    }

    // ---------------------------
    // Actor finder
    // ---------------------------
    uintptr_t findActor(const std::string& targetName,
        uintptr_t actorArray,
        uintptr_t gNames,
        int actorCount = 5000,
        int debugCount = 10);
};
