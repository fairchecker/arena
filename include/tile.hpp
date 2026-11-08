#pragma once

#include "types.hpp"

#include <optional>

struct Tile {
    bool walkable{true};
    int food{0};
    std::optional<EntityId> entityId;
    bool crate{false};
    bool foodSource{false};
};

struct TileView {
    Position position{};
    bool walkable{true};
    int food{0};
    bool crate{false};
    bool foodSource{false};
};
