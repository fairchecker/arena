#include "agent.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
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

GreedyFoodBrain::GreedyFoodBrain(unsigned int seed) : state_(seed ? seed : 1U) {}

unsigned int GreedyFoodBrain::next() {
    state_ ^= state_ << 13U;
    state_ ^= state_ >> 17U;
    state_ ^= state_ << 5U;
    return state_;
}

Action GreedyFoodBrain::decide(const Observation& observation) {
    const AgentView& self = observation.self;
    if (self.energy <= 0) return Action::wait();

    const auto tileAt = [&](Position position) -> const TileView* {
        const auto found = std::find_if(observation.visibleTiles.begin(), observation.visibleTiles.end(),
            [&](const TileView& tile) { return tile.position == position; });
        return found == observation.visibleTiles.end() ? nullptr : &*found;
    };
    const auto passable = [&](Position position) {
        const TileView* tile = tileAt(position);
        if (tile && (!tile->walkable || tile->crate)) return false;
        return std::none_of(observation.visibleEntities.begin(), observation.visibleEntities.end(),
            [&](const EntityView& entity) { return entity.position == position; });
    };

    const bool hungry = self.hunger >= 30 || self.health < self.maxHealth;
    const TileView* here = tileAt(self.position);
    if (hungry && here && here->food > 0) return Action::eat();

    if (hungry) {
        const TileView* target = nullptr;
        int bestDistance = 0;
        for (const TileView& tile : observation.visibleTiles) {
            if (tile.food <= 0 || tile.position == self.position) continue;
            const int distance = manhattanDistance(self.position, tile.position);
            if (!target || distance < bestDistance) { target = &tile; bestDistance = distance; }
        }
        if (target) {
            const int rowDelta = target->position.row - self.position.row;
            const int columnDelta = target->position.column - self.position.column;
            const Rotation vertical = rowDelta < 0 ? Rotation::Up : Rotation::Down;
            const Rotation horizontal = columnDelta < 0 ? Rotation::Left : Rotation::Right;
            std::array<Rotation, 2> order{{vertical, horizontal}};
            int count = 0;
            if (std::abs(rowDelta) >= std::abs(columnDelta)) {
                if (rowDelta != 0) order[static_cast<std::size_t>(count++)] = vertical;
                if (columnDelta != 0) order[static_cast<std::size_t>(count++)] = horizontal;
            } else {
                if (columnDelta != 0) order[static_cast<std::size_t>(count++)] = horizontal;
                if (rowDelta != 0) order[static_cast<std::size_t>(count++)] = vertical;
            }
            for (int i = 0; i < count; ++i) {
                const Rotation wanted = order[static_cast<std::size_t>(i)];
                if (!passable(self.position + getFromRotation(wanted))) continue;
                return wanted == self.direction ? Action::move() : Action::rotate(wanted);
            }
        }
    }

    // Wander: keep going while the way ahead is free, otherwise turn to a free side.
    if (passable(self.position + getFromRotation(self.direction)) && next() % 8U != 0U) return Action::move();
    const std::array<Rotation, 4> all{{Rotation::Up, Rotation::Right, Rotation::Down, Rotation::Left}};
    const std::size_t start = next() % 4U;
    for (std::size_t i = 0; i < all.size(); ++i) {
        const Rotation candidate = all[(start + i) % all.size()];
        if (candidate != self.direction && passable(self.position + getFromRotation(candidate)))
            return Action::rotate(candidate);
    }
    return Action::wait();
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
