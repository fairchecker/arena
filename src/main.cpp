#include "arena.hpp"

#include <iostream>
#include <memory>

namespace {
const char* actionName(ActionType type) {
    switch (type) {
        case ActionType::Wait: return "wait";
        case ActionType::Move: return "move";
        case ActionType::Rotate: return "rotate";
        case ActionType::Attack: return "attack";
        case ActionType::Eat: return "eat";
        case ActionType::Inspect: return "inspect";
    }
    return "unknown";
}
}

int main() {
    Arena arena(12, 8);
    arena.addFood({1, 1}, 4);
    arena.addFood({6, 10}, 4);

    const EntityId alpha = arena.addAgent(ArenaEntity(
        {2, 2}, std::make_unique<AggressiveBrain>(), Rotation::Right));
    const EntityId beta = arena.addAgent(ArenaEntity(
        {5, 9}, std::make_unique<RandomBrain>(42), Rotation::Left));

    if (alpha < 0 || beta < 0) {
        std::cerr << "Failed to create demo agents\n";
        return 1;
    }

    std::cout << "Arena demo: " << arena.getWorld().getWidth() << 'x'
              << arena.getWorld().getHeight() << "; agents " << alpha
              << " and " << beta << "\n";

    for (int i = 0; i < 30 && arena.aliveCount() > 0; ++i) {
        const TickReport report = arena.step();
        std::cout << "tick " << report.tick << " | alive: " << report.aliveAgents << '\n';
        for (const auto& record : report.actions) {
            std::cout << "  agent " << record.actorId << ": " << actionName(record.action.type)
                      << " -> " << (record.result.success ? "ok" : "failed")
                      << " (" << record.result.message << ")\n";
        }
    }
    return 0;
}
