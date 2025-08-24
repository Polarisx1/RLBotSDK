#include "MemoryReader.h"
#include <iostream>

uintptr_t MemoryReader::findActor(const std::string& targetName,
    uintptr_t actorArray,
    uintptr_t gNames,
    int actorCount,
    int debugCount) {
    if (!actorArray || !gNames) return 0;

    for (int i = 0; i < actorCount; i++) {
        uintptr_t actor = read<uintptr_t>(actorArray + i * sizeof(uintptr_t));
        if (!actor) continue;

        int nameId = read<int>(actor + 0x18);
        std::string name = getNameFromId(gNames, nameId);

        if (debugCount > 0 && i < debugCount) {
            std::cout << "[DBG] Actor[" << i << "] " << name
                << " at 0x" << std::hex << actor << std::dec << "\n";
        }

        if (name.find(targetName) != std::string::npos) {
            std::cout << "[+] Found " << targetName
                << " at 0x" << std::hex << actor << std::dec << "\n";
            return actor;
        }
    }

    std::cout << "[!] " << targetName << " not found.\n";
    return 0;
}
