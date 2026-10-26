#pragma once

#include "tile.hpp"
#include "types.hpp"
#include <vector>

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

    bool verifyPosition(Position position){
        if(position.column > width_ || position.row > height_ || position.column < 0 || position.row < 0) return false;
        return true; 
    }

    int updateTileObject(Position first, Position second) {
        if(!getTile(first).objectId.has_value()) return -1;
        getTile(second).objectId = getTile(first).objectId;
        getTile(first).objectId.reset(); 
    }

    private:
    int width_;
    int height_;
    std::vector<Tile> tiles_;
};