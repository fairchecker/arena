#pragma once

#include "tile.hpp"

#include <cstddef>
#include <vector>

class World {
public:
    World(int width, int height);

    int getWidth() const noexcept { return width_; }
    int getHeight() const noexcept { return height_; }

    bool inBounds(Position position) const noexcept;
    bool verifyPosition(Position position) const noexcept { return inBounds(position); }
    bool isWalkable(Position position) const noexcept;
    bool isOccupied(Position position) const noexcept;
    std::optional<EntityId> entityAt(Position position) const;

    const Tile& getTile(Position position) const;
    Tile& getTile(Position position);
    void setTile(const Tile& tile, Position position);
    bool setWalkable(Position position, bool walkable);
    bool addFood(Position position, int amount);
    bool consumeFood(Position position, int amount = 1);
    bool setCrate(Position position, bool present = true);
    bool pushCrate(EntityId id, Position from, Position direction);
    bool setFoodSource(Position position);
    void refillFoodSources();

    bool placeEntity(EntityId id, Position position);
    bool moveEntity(EntityId id, Position from, Position to);
    bool removeEntity(EntityId id, Position position);

private:
    std::size_t indexOf(Position position) const;

    int width_;
    int height_;
    std::vector<Tile> tiles_;
};
