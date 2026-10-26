#pragma once

#include "tile.hpp"
#include "arena.hpp"

class World {
    public:
    World(int width, int height) {
        width_ = width;
        height_ = height;
        tiles_ = std::vector<Tile>(width * height);
    }

    Tile getTile(Position position) {
        return tiles_[position.row * width_ + position.column];
    }

    void setTile(Tile tile, Position position) {
        tiles_[position.row * width_ + position.column] = tile;
    }

    private:
    int width_;
    int height_;
    std::vector<Tile> tiles_;
};