#include "MemoryReader.h"
#include <vector>
#include <string>
#include <cstring>
#include <cstdint>
#include <iostream>

// ---------------- Pattern scan over the main module ----------------
uintptr_t MemoryReader::patternScan(const char* pattern, const char* mask) const {
    if (!base_ || !moduleSize_) return 0;

    std::vector<uint8_t> buffer(moduleSize_);
    SIZE_T br = 0;
    if (!ReadProcessMemory(hProc_, (LPCVOID)base_, buffer.data(), moduleSize_, &br) || br < moduleSize_) {
        return 0;
    }
    size_t mlen = std::strlen(mask);
    for (size_t i = 0; i + mlen <= buffer.size(); ++i) {
        bool ok = true;
        for (size_t j = 0; j < mlen; ++j) {
            if (mask[j] == 'x' && buffer[i + j] != (uint8_t)pattern[j]) { ok = false; break; }
        }
        if (ok) return base_ + i;
    }
    return 0;
}

// ---------------- UE4/UE5 FNamePool-like (first) + UE3 chunked (fallback) ----------------
std::string MemoryReader::getNameFromId(uintptr_t gnamesAbs, int id) const {
    if (id < 0) return {};

    // UE4/UE5 heuristic
    {
        uintptr_t pool = read<uintptr_t>(gnamesAbs);
        if (!is_user_canonical(pool)) pool = gnamesAbs;

        uintptr_t blocks = read<uintptr_t>(pool + 0x10);
        if (!is_user_canonical(blocks)) blocks = pool + 0x10;

        uint32_t blockIndex = (uint32_t)id >> 16;
        uint32_t offsetIndex = (uint16_t)id;

        auto tryDecode = [&](int shift)->std::string {
            uintptr_t blockPtr = read<uintptr_t>(blocks + 8ULL * blockIndex);
            if (!is_user_canonical(blockPtr)) return {};
            uintptr_t entry = blockPtr + ((uintptr_t)offsetIndex << shift);

            uint16_t header = read<uint16_t>(entry);
            if (!header) return {};
            bool     isWide = (header & 1) != 0;
            uint16_t len = header >> 1;
            if (len == 0 || len > 128) return {};

            if (isWide) {
                std::vector<wchar_t> w(len + 1, L'\0');
                if (!readBytes(entry + 2, w.data(), len * sizeof(wchar_t))) return {};
                std::string out; out.reserve(len);
                for (uint16_t i = 0; i < len; ++i) {
                    wchar_t wc = w[i];
                    if (wc < 32 || wc > 126) return {};
                    out.push_back((char)wc);
                }
                return printable_ascii(out) ? out : std::string();
            }
            else {
                std::string out(len, '\0');
                if (!readBytes(entry + 2, out.data(), len)) return {};
                return printable_ascii(out) ? out : std::string();
            }
            };

        std::string s = tryDecode(1);
        if (!s.empty()) return s;
        s = tryDecode(2);
        if (!s.empty()) return s;
    }

    // UE3 fallback
    return tryUE3Name(gnamesAbs, id);
}

// ---- UE3 chunked GNames: Chunks[chunk][idx] -> FNameEntry*, ASCII zero-terminated ----
std::string MemoryReader::tryUE3Name(uintptr_t gnamesAbs, int id) const {
    if (id < 0) return {};

    // candidates for where "Chunks" pointer array might live relative to gnamesAbs
    const int OFFS[] = { 0x0, 0x10, 0x20, 0x30 };
    const int CHUNK_SIZE = 16384;

    int chunk = id / CHUNK_SIZE;
    int within = id % CHUNK_SIZE;

    for (int baseOff : OFFS) {
        uintptr_t chunksBase = read<uintptr_t>(gnamesAbs + baseOff);
        if (!is_user_canonical(chunksBase)) {
            // sometimes it's inline (not double-indirect)
            chunksBase = gnamesAbs + baseOff;
            if (!is_user_canonical(chunksBase)) continue;
        }

        uintptr_t chunkPtr = read<uintptr_t>(chunksBase + (uintptr_t)chunk * 8ULL);
        if (!is_user_canonical(chunkPtr)) continue;

        uintptr_t entry = read<uintptr_t>(chunkPtr + (uintptr_t)within * 8ULL);
        if (!is_user_canonical(entry)) continue;

        // Try to read a C-string at entry + {0x0, 0x10}
        auto readCStr = [&](uintptr_t p)->std::string {
            char buf[128] = { 0 };
            if (!readBytes(p, buf, sizeof(buf) - 1)) return {};
            buf[sizeof(buf) - 1] = 0;
            std::string out(buf);
            if (printable_ascii(out) && !out.empty()) return out;
            return {};
            };

        if (auto s = readCStr(entry); !s.empty()) return s;
        if (auto s = readCStr(entry + 0x10); !s.empty()) return s;
    }

    return {};
}

// ---------------- UWorld discovery ----------------
uintptr_t MemoryReader::findUWorldPattern() const {
    const char pat[] = "\x48\x8B\x1D\x00\x00\x00\x00\x48\x85\xDB\x74";
    const char mask[] = "xxx????xxxx";

    uintptr_t at = patternScan(pat, mask);
    if (!at) return 0;

    int32_t rel = read<int32_t>(at + 3);
    uintptr_t ptrLoc = at + 7 + rel;
    uintptr_t uWorld = read<uintptr_t>(ptrLoc);

    if (is_user_canonical(uWorld)) {
        std::cout << "[+] UWorld (pattern) -> 0x" << std::hex << uWorld << std::dec << "\n";
        return uWorld;
    }
    return 0;
}

// ---- Robust TArray resolver (works for many layouts) ----
bool MemoryReader::resolveTArrayAt(uintptr_t arrayAddr, uintptr_t& data, int& num) const {
    // Layout A: Data(+0), Num(+8), Max(+12)
    data = read<uintptr_t>(arrayAddr + 0x0);
    int32_t numA = read<int32_t>(arrayAddr + 0x8);
    int32_t maxA = read<int32_t>(arrayAddr + 0xC);
    if (is_user_canonical(data) && numA >= 0 && numA <= 100000 && maxA >= numA && maxA <= 200000) {
        num = numA;
        return true;
    }
    // Layout B: Data(+0), pad(+8), Num(+16), Max(+20)
    data = read<uintptr_t>(arrayAddr + 0x0);
    int32_t numB = read<int32_t>(arrayAddr + 0x10);
    int32_t maxB = read<int32_t>(arrayAddr + 0x14);
    if (is_user_canonical(data) && numB >= 0 && numB <= 100000 && maxB >= numB && maxB <= 200000) {
        num = numB;
        return true;
    }
    return false;
}

// ---- Figure out where the real GObjects array lives (Data & Num) ----
bool MemoryReader::resolveGObjects(uintptr_t gObjectsAbs, uintptr_t& data, int& num) const {
    // Try at GObjects itself
    if (resolveTArrayAt(gObjectsAbs, data, num)) return true;

    // Try common struct wrappers (the TArray stored at an inner offset)
    const int OFFS[] = { 0x8, 0x10, 0x18, 0x20, 0x28 };
    for (int off : OFFS) {
        if (resolveTArrayAt(gObjectsAbs + off, data, num)) return true;
        uintptr_t inner = read<uintptr_t>(gObjectsAbs + off);
        if (is_user_canonical(inner) && resolveTArrayAt(inner, data, num)) return true;
    }
    return false;
}

uintptr_t MemoryReader::findUWorldByObjects(uintptr_t gObjectsAbs, uintptr_t gnamesAbs, int scanLimit) const {
    uintptr_t data = 0; int num = 0;
    if (!resolveGObjects(gObjectsAbs, data, num)) {
        std::cout << "[!] GObjects array could not be resolved.\n";
        return 0;
    }
    int limit = (scanLimit > 0 && scanLimit < num) ? scanLimit : num;

    for (int i = 0; i < limit; ++i) {
        uintptr_t obj = read<uintptr_t>(data + (uintptr_t)i * 8ULL);
        if (!is_user_canonical(obj)) continue;
        int nameId = read<int>(obj + 0x18);
        std::string nm = getNameFromId(gnamesAbs, nameId);
        if (!nm.empty() && nm.find("World") != std::string::npos) {
            std::cout << "[+] UWorld (GObjects) -> 0x" << std::hex << obj << std::dec
                << " name=" << nm << "\n";
            return obj;
        }
    }
    return 0;
}

// ---------------- Actor array discovery (unchanged concept, permissive) ----------------
bool MemoryReader::findActorArray(uintptr_t uWorld,
    uintptr_t gnamesAbs,
    uintptr_t& outPersistentLevel,
    uintptr_t& outActorArrayDataPtr,
    int& outActorCount) const {
    outPersistentLevel = 0;
    outActorArrayDataPtr = 0;
    outActorCount = 0;

    if (!is_user_canonical(uWorld)) return false;

    for (int woff = 0x20; woff <= 0x400; woff += 8) {
        uintptr_t candLevel = read<uintptr_t>(uWorld + woff);
        if (!is_user_canonical(candLevel)) continue;

        for (int aoff = 0x40; aoff <= 0x500; aoff += 8) {
            uintptr_t data = 0; int num = 0;
            if (!resolveTArrayAt(candLevel + aoff, data, num)) continue;

            // Validate by sampling names
            int samples = num < 48 ? num : 48;
            int step = samples / 12; if (step < 1) step = 1;

            int good = 0, seen = 0;
            for (int i = 0; i < samples; i += step) {
                uintptr_t act = read<uintptr_t>(data + (uintptr_t)i * 8ULL);
                if (!is_user_canonical(act)) continue;
                ++seen;

                int nameId = read<int>(act + 0x18);
                std::string nm = getNameFromId(gnamesAbs, nameId);
                if (!nm.empty() && printable_ascii(nm) && nm.size() <= 96) {
                    ++good;
                    if (good >= 3) break;
                }
                else {
                    // cheap rescue: scan a few 4-byte slots
                    bool rescued = false;
                    for (int k = 0x10; k <= 0x40; k += 4) {
                        int tryId = read<int>(act + k);
                        std::string tryNm = getNameFromId(gnamesAbs, tryId);
                        if (!tryNm.empty() && printable_ascii(tryNm) && tryNm.size() <= 96) {
                            rescued = true; break;
                        }
                    }
                    if (rescued) ++good;
                }
            }

            if (good >= 3) {
                outPersistentLevel = candLevel;
                outActorArrayDataPtr = data;
                outActorCount = num;
                std::cout << "[+] ActorArray @Level+0x" << std::hex << aoff
                    << " data=0x" << outActorArrayDataPtr
                    << " count=" << std::dec << outActorCount << "\n";
                return true;
            }
        }
    }
    std::cout << "[!] No valid ActorArray found.\n";
    return false;
}

int MemoryReader::peekArrayCount(uintptr_t persistentLevel, uintptr_t actorArrayDataPtr) const {
    if (!is_user_canonical(persistentLevel) || !is_user_canonical(actorArrayDataPtr)) return -1;

    for (int aoff = 0x40; aoff <= 0x500; aoff += 8) {
        uintptr_t data = read<uintptr_t>(persistentLevel + aoff);
        if (data == actorArrayDataPtr) {
            int32_t numA = read<int32_t>(persistentLevel + aoff + 0x8);
            if (numA >= 0 && numA <= 65536) return numA;
            int32_t numB = read<int32_t>(persistentLevel + aoff + 0x10);
            if (numB >= 0 && numB <= 65536) return numB;
        }
    }
    return -1;
}

// ---------------- Actor finder (via ActorArray) ----------------
uintptr_t MemoryReader::findActor(const std::string& targetName,
    uintptr_t actorArrayDataPtr,
    uintptr_t gnamesAbs,
    int actorCount,
    int debugN) const {
    if (!is_user_canonical(actorArrayDataPtr)) return 0;
    if (actorCount <= 0 || actorCount > 100000) return 0;

    int dbg = (debugN < actorCount ? debugN : actorCount);
    for (int i = 0; i < actorCount; ++i) {
        uintptr_t act = read<uintptr_t>(actorArrayDataPtr + (uintptr_t)i * 8ULL);
        if (!is_user_canonical(act)) continue;

        int nameId = read<int>(act + 0x18);
        std::string nm = getNameFromId(gnamesAbs, nameId);

        if (i < dbg) {
            std::cout << "[DBG] Actor[" << i << "] " << (nm.empty() ? "<noname>" : nm)
                << " at 0x" << std::hex << act << std::dec << "\n";
        }
        if (!nm.empty()) {
            if (nm.find(targetName) != std::string::npos) return act;
            if (targetName == "Ball_TA" && nm.find("Ball") != std::string::npos) return act;
            if (targetName == "Car_TA" &&
                (nm.find("Car") != std::string::npos || nm.find("Vehicle") != std::string::npos))
                return act;
        }
    }
    return 0;
}

// ---------------- Fallback: scan GObjects directly (now using Data & Num) ----------------
uintptr_t MemoryReader::findActorViaObjects(const std::string& targetName,
    uintptr_t gObjectsAbs,
    uintptr_t gnamesAbs,
    int scanLimit,
    int debugEvery) const {
    uintptr_t data = 0; int num = 0;
    if (!resolveGObjects(gObjectsAbs, data, num)) {
        std::cout << "[!] GObjects array could not be resolved.\n";
        return 0;
    }
    int limit = (scanLimit > 0 && scanLimit < num) ? scanLimit : num;

    int printed = 0;
    for (int i = 0; i < limit; ++i) {
        uintptr_t obj = read<uintptr_t>(data + (uintptr_t)i * 8ULL);
        if (!is_user_canonical(obj)) continue;

        int nameId = read<int>(obj + 0x18);
        std::string nm = getNameFromId(gnamesAbs, nameId);

        if (debugEvery > 0 && (i % debugEvery == 0) && printed < 60) {
            std::cout << "[DBG][GObj] i=" << i << " name="
                << (nm.empty() ? "<noname>" : nm)
                << " ptr=0x" << std::hex << obj << std::dec << "\n";
            ++printed;
        }

        if (!nm.empty()) {
            if (nm.find(targetName) != std::string::npos) return obj;
            if (targetName == "Ball_TA" && nm.find("Ball") != std::string::npos) return obj;
            if (targetName == "Car_TA" &&
                (nm.find("Car") != std::string::npos || nm.find("Vehicle") != std::string::npos))
                return obj;
        }
    }
    return 0;
}

// ---------------- Entity readers (offsets are placeholders; adjust as needed) ----------------
BallState MemoryReader::getBall(uintptr_t ballAddr) const {
    BallState ball{};
    ball.pos = readVec3(ballAddr + 0x11C);
    ball.vel = readVec3(ballAddr + 0x124);
    return ball;
}

CarState MemoryReader::getCar(uintptr_t carAddr) const {
    CarState car{};
    car.pos = readVec3(carAddr + 0x11C);
    car.vel = readVec3(carAddr + 0x124);
    car.rotation.pitch = read<float>(carAddr + 0x148);
    car.rotation.yaw = read<float>(carAddr + 0x14C);
    car.rotation.roll = read<float>(carAddr + 0x150);
    car.boost = read<float>(carAddr + 0x0AB0);
    car.isDemolished = read<bool>(carAddr + 0x07F0);
    return car;
}
