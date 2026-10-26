#pragma once

struct InspectInfo {
    int health;
    ObjectType type;  
};

enum ObjectType {
    Entity,
    Resource,
    Unknown
};

struct Position {
    int row;
    int column;

    Position& operator+=(const Position& other) {
        row += other.row;
        column += other.column;
        return *this;
    }
};

enum Rotation {
    Up,
    Right,
    Down,
    Left
};

Position getFromRotation(Rotation rotation) {
    switch(rotation) {
        case Rotation::Up:
            return Position { 0, 1 };
            break;
        case Rotation::Right:
            return Position { 1, 0 };
            break;
        case Rotation::Down: 
            return Position { 0, -1 };
            break;
        case Rotation::Left:
            return Position { -1, 0 };
            break;
    }
    return Position { 0, 0 };
}