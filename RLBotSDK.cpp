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

uintptr_t GetModuleBaseAddress(DWORD procId, const std::wstring& modName) {
    uintptr_t modBaseAddr = 0;
    HANDLE hSnap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, procId);
    if (hSnap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W modEntry{ sizeof(modEntry) };
        if (Module32FirstW(hSnap, &modEntry)) {
            do {
                if (!_wcsicmp(modEntry.szModule, modName.c_str())) {
                    modBaseAddr = reinterpret_cast<uintptr_t>(modEntry.modBaseAddr);
                    break;
                }
            } while (Module32NextW(hSnap, &modEntry));
        }
    }
    CloseHandle(hSnap);
    return modBaseAddr;
}

int main() {
    DWORD pid = GetProcessIdByName(L"RocketLeague.exe");
    if (!pid) {
        std::cout << "[!] Rocket League not running.\n";
        return 1;
    }

    HANDLE hProc = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    uintptr_t base = GetModuleBaseAddress(pid, L"RocketLeague.exe");

    std::cout << "[*] Attached to Rocket League (PID " << pid << ")\n";
    std::cout << "[*] Base address: 0x" << std::hex << base << std::dec << "\n";

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
    std::cout << "[+] Controller absolute: 0x" << std::hex << controllerAddr << "\n";

    BotManager manager;
    manager.addBot(std::make_unique<DummyBot>());
    manager.addBot(std::make_unique<NextoBot>());

    std::cout << "[*] Bot framework ready. Use F1/F2 to select, F3 to toggle.\n";

    MemoryReader reader(hProc);

    uintptr_t uWorld = reader.findUWorld(base, gobjectsAbs);
    if (!uWorld) {
        std::cout << "[!] UWorld not found.\n";
        return 1;
    }
    std::cout << "[+] UWorld (pattern) at 0x" << std::hex << uWorld << "\n";

    // Debug: Try to resolve ActorArray
    uintptr_t actorArray = reader.debugFindActorArray(uWorld);  // ✅ fixed name
    if (!actorArray) {
        std::cout << "[!] Could not resolve ActorArray.\n";
        return 1;
    }

    while (true) {
        GameState game;

        // Example: search for Ball and Car
        uintptr_t ballAddr = reader.findActor("Ball_TA", actorArray, gnamesAbs);
        uintptr_t carAddr = reader.findActor("Car_TA", actorArray, gnamesAbs);

        if (ballAddr && carAddr) {
            game.ball = reader.getBall(ballAddr);
            game.cars.clear();
            game.cars.push_back(reader.getCar(carAddr));
            game.car = game.cars[0];
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
