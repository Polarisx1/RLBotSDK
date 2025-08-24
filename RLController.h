#pragma once
#include <Windows.h>

// Struct that Rocket League expects in memory
struct RLController {
    float throttle = 0.0f;
    float steer = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    BYTE handbrake = 0;
    BYTE jump = 0;
    BYTE boost = 0;
};
