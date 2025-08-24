#pragma once
#include "IBot.h"
#include "GameState.h"
#include <iostream>
#include <cmath>
#include <algorithm> // for std::clamp

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class NextoBot : public IBot {
public:
    void initialize() override {
        std::cout << "[NextoBot] Initialized!\n";
    }

    ControllerState tick(const GameState& game) override {
        ControllerState state{};

        if (!game.cars.empty()) {
            const CarState& car = game.car;
            const BallState& ball = game.ball;

            // Direction vector
            float dx = ball.position.x - car.position.x;
            float dy = ball.position.y - car.position.y;
            float angleToBall = atan2f(dy, dx);

            float carAngle = car.rotation.yaw;
            float diff = angleToBall - carAngle;

            // Normalize angle difference
            while (diff > M_PI)  diff -= 2.0f * static_cast<float>(M_PI);
            while (diff < -M_PI) diff += 2.0f * static_cast<float>(M_PI);

            state.throttle = 1.0f;
            state.steer = std::clamp(diff, -1.0f, 1.0f);
        }

        return state;
    }
};
