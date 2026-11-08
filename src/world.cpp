#include "world.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace {
std::size_t checkedTileCount(int width, int height) {
    if (width <= 0 || height <= 0) {
        throw std::invalid_argument("World dimensions must be positive");
    }
    const auto unsignedWidth = static_cast<std::size_t>(width);
    const auto unsignedHeight = static_cast<std::size_t>(height);
    if (unsignedHeight > std::numeric_limits<std::size_t>::max() / unsignedWidth) {
        throw std::length_error("World dimensions exceed addressable memory");
    }
    return unsignedWidth * unsignedHeight;
}
}

World::World(int width, int height)
    : width_(width), height_(height), tiles_(checkedTileCount(width, height)) {}

bool World::inBounds(Position position) const noexcept {
    return position.row >= 0 && position.row < height_ &&
           position.column >= 0 && position.column < width_;
}

bool World::isWalkable(Position position) const noexcept {
    return inBounds(position) && tiles_[indexOf(position)].walkable && !tiles_[indexOf(position)].crate;
}

bool World::isOccupied(Position position) const noexcept {
    return inBounds(position) && tiles_[indexOf(position)].entityId.has_value();
}

std::optional<EntityId> World::entityAt(Position position) const {
    if (!inBounds(position)) return std::nullopt;
    return getTile(position).entityId;
}

std::size_t World::indexOf(Position position) const {
    if (!inBounds(position)) throw std::out_of_range("Position is outside the world");
    return static_cast<std::size_t>(position.row) * static_cast<std::size_t>(width_) +
           static_cast<std::size_t>(position.column);
}

const Tile& World::getTile(Position position) const {
    return tiles_.at(indexOf(position));
}

Tile& World::getTile(Position position) {
    return tiles_.at(indexOf(position));
}

void World::setTile(const Tile& tile, Position position) {
    Tile& current = getTile(position);
    if (tile.food < 0) throw std::invalid_argument("Tile food cannot be negative");
    if ((current.entityId && (!tile.walkable || tile.crate)) || (tile.crate && !tile.walkable)) {
        throw std::invalid_argument("Cannot make an occupied tile non-walkable");
    }
    Tile replacement = tile;
    // Occupancy is managed through placeEntity/moveEntity/removeEntity only.
    replacement.entityId = current.entityId;
    current = replacement;
}

bool World::setWalkable(Position position, bool walkable) {
    if (!inBounds(position) || ((!walkable) && (isOccupied(position) || getTile(position).crate))) return false;
    getTile(position).walkable = walkable;
    return true;
}

bool World::addFood(Position position, int amount) {
    if (!inBounds(position) || amount <= 0) return false;
    Tile& tile = getTile(position);
    if (amount > std::numeric_limits<int>::max() - tile.food) return false;
    tile.food += amount;
    return true;
}

bool World::consumeFood(Position position, int amount) {
    if (!inBounds(position) || amount <= 0) return false;
    Tile& tile = getTile(position);
    if (tile.food < amount) return false;
    tile.food -= amount;
    return true;
}

bool World::setCrate(Position position, bool present) {
    if (!inBounds(position)) return false;
    Tile& tile = getTile(position);
    if (present && (!tile.walkable || tile.entityId || tile.crate)) return false;
    tile.crate = present;
    return true;
}

bool World::pushCrate(EntityId id, Position from, Position direction) {
    if (direction != Position{-1, 0} && direction != Position{1, 0} &&
        direction != Position{0, -1} && direction != Position{0, 1}) return false;
    const Position cratePosition = from + direction;
    const Position destination = cratePosition + direction;
    if (!inBounds(from) || !inBounds(cratePosition) || !isWalkable(destination) || isOccupied(destination)) return false;
    Tile& source = getTile(from);
    Tile& crate = getTile(cratePosition);
    if (!source.entityId || *source.entityId != id || !crate.crate || crate.entityId) return false;
    crate.crate = false;
    getTile(destination).crate = true;
    source.entityId.reset();
    crate.entityId = id;
    return true;
}

bool World::setFoodSource(Position position) {
    if (!inBounds(position) || !getTile(position).walkable) return false;
    getTile(position).foodSource = true;
    return true;
}

void World::refillFoodSources() {
    for (auto& tile : tiles_) {
        if (tile.foodSource && tile.walkable && !tile.crate && tile.food == 0) tile.food = 1;
    }
}

bool World::placeEntity(EntityId id, Position position) {
    if (id < 0 || !isWalkable(position) || isOccupied(position)) return false;
    getTile(position).entityId = id;
    return true;
}

bool World::moveEntity(EntityId id, Position from, Position to) {
    if (!inBounds(from) || !isWalkable(to) || isOccupied(to)) return false;
    Tile& source = getTile(from);
    if (!source.entityId || *source.entityId != id) return false;
    source.entityId.reset();
    getTile(to).entityId = id;
    return true;
}

bool World::removeEntity(EntityId id, Position position) {
    if (!inBounds(position)) return false;
    Tile& tile = getTile(position);
    if (!tile.entityId || *tile.entityId != id) return false;
    tile.entityId.reset();
    return true;
}
