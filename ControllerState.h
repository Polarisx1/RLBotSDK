#pragma once

struct ControllerState {
    float throttle = 0.0f;
    float steer = 0.0f;
    float pitch = 0.0f;
    float yaw = 0.0f;
    float roll = 0.0f;
    bool handbrake = false;
    bool jump = false;
    bool boost = false;
};
