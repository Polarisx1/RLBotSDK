#pragma once
#include <vector>
#include "Vec3.h"

struct Rotation {
    float pitch{ 0.f };
    float yaw{ 0.f };
    float roll{ 0.f };
};

struct BallState {
    Vec3 pos{};
    Vec3 vel{};
};

struct CarState {
    Vec3 pos{};
    Vec3 vel{};
    Rotation rotation{};
    float boost{ 0.f };
    bool isDemolished{ false };
};

struct GameState {
    BallState ball{};
    CarState  car{};
    std::vector<CarState> cars{};
};

// Legacy aliases so older code using .position/.velocity still works
#ifndef RLSDK_NO_LEGACY_ALIASES
#define position pos
#define velocity vel
#endif
