#include <windows.h>
#include <tlhelp32.h>
#include <iostream>
#include <fstream>
#include "json.hpp"

// Bot system
#include "BotManager.h"
#include "DummyBot.h"
#include "NextoBot.h"
#include "RLController.h"
#include "MemoryUtils.h"
#include "MemoryReader.h"

using json = nlohmann::json;

DWORD GetProcessIdByName(const std::wstring& processName) {
    PROCESSENTRY32W entry{ sizeof(entry) };
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (!_wcsicmp(entry.szExeFile, processName.c_str())) {
                CloseHandle(snapshot);
                return entry.th32ProcessID;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return 0;
}

bool GetModuleBaseAndSize(DWORD procId, const std::wstring& modName, uintptr_t& base, size_t& size) {
    base = 0; size = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap == INVALID_HANDLE_VALUE) return false;
    MODULEENTRY32W me{ sizeof(me) };
    if (Module32FirstW(hSnap, &me)) {
        do {
            if (!_wcsicmp(me.szModule, modName.c_str())) {
                base = reinterpret_cast<uintptr_t>(me.modBaseAddr);
                size = static_cast<size_t>(me.modBaseSize);
                CloseHandle(hSnap);
                return true;
            }
        } while (Module32NextW(hSnap, &me));
    }
    CloseHandle(hSnap);
    return false;
}

int main() {
    DWORD pid = GetProcessIdByName(L"RocketLeague.exe");
    if (!pid) {
        std::cout << "[!] Rocket League not running.\n";
        return 1;
    }

    uintptr_t base = 0; size_t modSize = 0;
    if (!GetModuleBaseAndSize(pid, L"RocketLeague.exe", base, modSize)) {
        std::cout << "[!] Could not get module info.\n";
        return 1;
    }

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);

    std::cout << "[*] Attached to Rocket League (PID " << pid << ")\n";
    std::cout << "[*] Base address: 0x" << std::hex << base << " Size: 0x" << modSize << std::dec << "\n";

    // Load offsets
    std::ifstream f("offsets.json");
    if (!f.is_open()) {
        std::cout << "[!] Missing offsets.json\n";
        return 1;
    }
    json j; f >> j;
    uintptr_t gnamesOffset = j["epic"]["gnames_offset"];
    uintptr_t gobjectsOffset = j["epic"]["gobjects_offset"];
    uintptr_t controllerOffset = j["epic"]["controller_input_offset"];

    uintptr_t gnamesAbs = base + gnamesOffset;
    uintptr_t gobjectsAbs = base + gobjectsOffset;
    uintptr_t controllerAddr = base + controllerOffset;

    std::cout << "[+] GNames absolute: 0x" << std::hex << gnamesAbs << "\n";
    std::cout << "[+] GObjects absolute: 0x" << std::hex << gobjectsAbs << "\n";
    std::cout << "[+] Controller absolute: 0x" << std::hex << controllerAddr << std::dec << "\n";

    BotManager manager;
    manager.addBot(std::make_unique<DummyBot>());
    manager.addBot(std::make_unique<NextoBot>());
    std::cout << "[*] Bot framework ready. Use F1/F2 to select, F3 to toggle.\n";

    MemoryReader reader(hProc, base, modSize);

    // 1) Find UWorld by pattern; if that fails, fallback to GObjects scan
    uintptr_t uWorld = reader.findUWorldPattern();
    if (!uWorld) {
        uWorld = reader.findUWorldByObjects(gobjectsAbs, gnamesAbs, 800000);
    }
    if (!uWorld) {
        std::cout << "[!] UWorld not found.\n";
        return 1;
    }
    std::cout << "[+] UWorld at 0x" << std::hex << uWorld << std::dec << "\n";

    // 2) Try to resolve actor array (PersistentLevel -> Actors)
    uintptr_t persistentLevel = 0;
    uintptr_t actorArrayData = 0;
    int       actorCount = 0;

    bool haveArray = reader.findActorArray(uWorld, gnamesAbs, persistentLevel, actorArrayData, actorCount);
    if (!haveArray) {
        std::cout << "[!] Could not resolve ActorArray. Will fallback to GObjects scanning.\n";
    }

    // Main loop
    while (true) {
        GameState game;

        uintptr_t ballAddr = 0, carAddr = 0;

        if (haveArray) {
            int freshCount = reader.peekArrayCount(persistentLevel, actorArrayData);
            if (freshCount > 0) actorCount = freshCount;

            ballAddr = reader.findActor("Ball_TA", actorArrayData, gnamesAbs, actorCount, 8);
            carAddr = reader.findActor("Car_TA", actorArrayData, gnamesAbs, actorCount, 8);

            // If array stopped working at runtime, auto-fallback
            if ((!ballAddr || !carAddr)) {
                ballAddr = reader.findActorViaObjects("Ball_TA", gobjectsAbs, gnamesAbs, 800000);
                carAddr = reader.findActorViaObjects("Car_TA", gobjectsAbs, gnamesAbs, 800000);
            }
        }
        else {
            // Hard fallback: scan GObjects every frame
            ballAddr = reader.findActorViaObjects("Ball_TA", gobjectsAbs, gnamesAbs, 800000, 0);
            carAddr = reader.findActorViaObjects("Car_TA", gobjectsAbs, gnamesAbs, 800000, 0);
        }

        if (ballAddr && carAddr) {
            game.ball = reader.getBall(ballAddr);
            game.cars.clear();
            game.cars.push_back(reader.getCar(carAddr));
            game.car = game.cars[0];
        }
        else {
            std::cout << "[WARN] Could not resolve Ball/Car this frame.\n";
        }

        ControllerState input = manager.run(game);

        RLController rlInput{};
        rlInput.throttle = input.throttle;
        rlInput.steer = input.steer;
        rlInput.pitch = input.pitch;
        rlInput.yaw = input.yaw;
        rlInput.roll = input.roll;
        rlInput.handbrake = input.handbrake ? 1 : 0;
        rlInput.jump = input.jump ? 1 : 0;
        rlInput.boost = input.boost ? 1 : 0;

        WriteMemory(hProc, controllerAddr, rlInput);
        Sleep(16);
    }

    return 0;
}
