#pragma once
#include "Vec3.h"
#include <vector>

struct BallState {
    Vec3 position;
    Vec3 velocity;
};

struct CarState {
    Vec3 position;
    Vec3 velocity;
    struct {
        float pitch;
        float yaw;
        float roll;
    } rotation;
    bool isDemolished = false;
    float boost = 0.0f;
};

struct GameState {
    BallState ball;
    std::vector<CarState> cars;
    CarState car; // convenience: "our" car
};
