#pragma once
#include "ControllerState.h"
#include "GameState.h"

// Base interface for all bots (Nexto, Dummy, etc.)
class IBot {
public:
    virtual ~IBot() = default;

    // Called once when the bot is enabled
    virtual void initialize() = 0;

    // Called every tick with the current game state
    virtual ControllerState tick(const GameState& game) = 0;
};
