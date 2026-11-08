#include "arena.hpp"
#include "graph_brain.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <algorithm>
#include <iostream>
#include <memory>

void exposeCorridor(GraphBrain& brain) {
    // Controlled familiarization: observations from a guided walk, with range 1.
    // Source location is seen at the start, not visible at the evaluation start.
    for (int column = 0; column <= 4; ++column) {
        Observation o;
        o.self.position = {0, column};
        o.self.direction = Rotation::Right;
        o.self.health = o.self.maxHealth = 10;
        o.self.energy = o.self.maxEnergy = 5;
        o.self.visionRange = 1;
        for (int c = std::max(0, column - 1); c <= std::min(6, column + 1); ++c)
            o.visibleTiles.push_back({{0, c}, true, c == 0 ? 1 : 0, false, c == 0});
        brain.decide(o);
    }
}

int returnToSource(bool remember, bool modulation) {
    auto brain = std::make_unique<GraphBrain>(modulation);
    exposeCorridor(*brain);
    if (!remember) brain->forgetResources();
    Arena arena(7, 1);
    arena.addFoodSource({0, 0});
    arena.addFood({0, 0});
    ArenaEntity agent({0, 4}, std::move(brain), Rotation::Right, 10, 2, 5, 1);
    for (int i = 0; i < 80; ++i) agent.advanceHunger();
    arena.addAgent(std::move(agent));
    for (int tick = 1; tick <= 40; ++tick) {
        const auto report = arena.step();
        for (const auto& action : report.actions)
            if (action.action.type == ActionType::Eat && action.result.success) return tick;
    }
    return 41;
}

int main() {
    const int full = returnToSource(true, true);
    const int forgotten = returnToSource(false, true);
    const int unmodulated = returnToSource(true, false);
    std::cout << "ticks to eat: memory=" << full << " forgotten=" << forgotten
              << " no-modulation=" << unmodulated << '\n';
    assert(full == 6);
    assert(full < forgotten && full < unmodulated);

    // Unknown/disconnected space is never treated as a traversable graph edge.
    GraphBrain blocked;
    Observation o;
    o.self.position = {0, 2};
    o.self.direction = Rotation::Left;
    o.self.health = o.self.maxHealth = 10;
    o.self.energy = o.self.maxEnergy = 5;
    o.self.hunger = 90;
    o.self.maxHunger = 100;
    o.visibleTiles = {{{0, 0}, true, 1, false, true}, {{0, 1}, false, 0}, {{0, 2}, true, 0}};
    assert(blocked.decide(o).type == ActionType::Wait);
    assert(blocked.diagnostics().propagationRounds <= 256);

    // A remembered depleted source is waited at, and becomes edible on refill.
    o.self.position = {0, 0};
    o.visibleTiles = {{{0, 0}, true, 0, false, true}};
    assert(blocked.decide(o).type == ActionType::Wait);
    o.visibleTiles[0].food = 1;
    assert(blocked.decide(o).type == ActionType::Eat);
}
