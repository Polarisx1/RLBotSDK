#pragma once
#include "IBot.h"
#include <iostream>

class DummyBot : public IBot {
public:
    void initialize() override {
        std::cout << "[DummyBot] Initialized!\n";
    }

    ControllerState tick(const GameState& game) override {
        ControllerState state;
        state.throttle = 1.0f; // Always drive forward
        return state;
    }
};
