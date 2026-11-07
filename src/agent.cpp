#include "agent.hpp"

#include <algorithm>
#include <cstdlib>

RandomBrain::RandomBrain(unsigned int seed) : state_(seed ? seed : 1U) {}

Action RandomBrain::decide(const Observation& observation) {
    if (observation.self.energy <= 0) return Action::wait();
    // Small deterministic PRNG: a run is reproducible for a fixed seed.
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 17U;
    state_ ^= state_ << 5U;

    if (!observation.visibleEntities.empty() && (state_ % 5U == 0U)) {
        const auto& target = observation.visibleEntities[state_ % observation.visibleEntities.size()];
        return Action::inspect(target.position);
    }

    switch (state_ % 5U) {
        case 0: return Action::move();
        case 1: return Action::rotate(rotateLeft(observation.self.direction));
        case 2: return Action::rotate(rotateRight(observation.self.direction));
        case 3: return Action::eat();
        default: return Action::wait();
    }
}

Action AggressiveBrain::decide(const Observation& observation) {
    if (observation.self.health <= 0 || observation.self.energy <= 0) return Action::wait();

    // Prefer the closest visible opponent; ties are resolved by world position.
    const EntityView* target = nullptr;
    for (const auto& candidate : observation.visibleEntities) {
        if (!target || candidate.distance < target->distance ||
            (candidate.distance == target->distance &&
             (candidate.position.row < target->position.row ||
              (candidate.position.row == target->position.row &&
               candidate.position.column < target->position.column)))) {
            target = &candidate;
        }
    }

    if (!target) {
        const auto here = std::find_if(observation.visibleTiles.begin(), observation.visibleTiles.end(),
            [&](const TileView& tile) { return tile.position == observation.self.position; });
        if (here != observation.visibleTiles.end() && here->food > 0) return Action::eat();
        return Action::move();
    }

    const int rowDelta = target->position.row - observation.self.position.row;
    const int colDelta = target->position.column - observation.self.position.column;
    Rotation desired = observation.self.direction;
    if (std::abs(rowDelta) >= std::abs(colDelta) && rowDelta != 0) {
        desired = rowDelta < 0 ? Rotation::Up : Rotation::Down;
    } else if (colDelta != 0) {
        desired = colDelta < 0 ? Rotation::Left : Rotation::Right;
    }

    if (target->distance == 1) {
        if (observation.self.direction == desired) return Action::attack();
        return Action::rotate(desired);
    }
    if (observation.self.direction != desired) return Action::rotate(desired);
    return Action::move();
}
