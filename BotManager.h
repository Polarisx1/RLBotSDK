#pragma once
#include <memory>
#include <vector>
#include "IBot.h"
#include "GameState.h"

class BotManager {
private:
    std::vector<std::unique_ptr<IBot>> bots;
    int activeIndex = -1;
    bool enabled = false;

public:
    void addBot(std::unique_ptr<IBot> bot) {
        bots.push_back(std::move(bot));
    }

    void toggleBot(int index) {
        if (index >= 0 && index < (int)bots.size()) {
            if (activeIndex == index && enabled) {
                enabled = false;
                std::cout << "[BotManager] Disabled bot index " << index << "\n";
            }
            else {
                activeIndex = index;
                enabled = true;
                bots[activeIndex]->initialize();
                std::cout << "[BotManager] Enabled bot index " << index << "\n";
            }
        }
    }

    ControllerState run(const GameState& game) {
        if (enabled && activeIndex >= 0 && activeIndex < (int)bots.size()) {
            return bots[activeIndex]->tick(game);
        }
        return ControllerState{};
    }
};
