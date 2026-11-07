#pragma once

#include <cstdlib>

using EntityId = int;

// Grid coordinates: row 0 is the top; column 0 is the left.
struct Position {
    int row{0};
    int column{0};

    Position& operator+=(const Position& other) noexcept {
        row += other.row;
        column += other.column;
        return *this;
    }
};

inline Position operator+(Position lhs, const Position& rhs) noexcept {
    lhs += rhs;
    return lhs;
}

inline bool operator==(const Position& lhs, const Position& rhs) noexcept {
    return lhs.row == rhs.row && lhs.column == rhs.column;
}

inline bool operator!=(const Position& lhs, const Position& rhs) noexcept {
    return !(lhs == rhs);
}

enum class Rotation {
    Up,
    Right,
    Down,
    Left
};

inline bool isValidRotation(Rotation rotation) noexcept {
    switch (rotation) {
        case Rotation::Up:
        case Rotation::Right:
        case Rotation::Down:
        case Rotation::Left:
            return true;
    }
    return false;
}

inline Position getFromRotation(Rotation rotation) noexcept {
    switch (rotation) {
        case Rotation::Up:    return {-1, 0};
        case Rotation::Right: return {0, 1};
        case Rotation::Down:  return {1, 0};
        case Rotation::Left:  return {0, -1};
    }
    return {0, 0};
}

inline Rotation rotateLeft(Rotation rotation) noexcept {
    switch (rotation) {
        case Rotation::Up: return Rotation::Left;
        case Rotation::Left: return Rotation::Down;
        case Rotation::Down: return Rotation::Right;
        case Rotation::Right: return Rotation::Up;
    }
    return Rotation::Up;
}

inline Rotation rotateRight(Rotation rotation) noexcept {
    switch (rotation) {
        case Rotation::Up: return Rotation::Right;
        case Rotation::Right: return Rotation::Down;
        case Rotation::Down: return Rotation::Left;
        case Rotation::Left: return Rotation::Up;
    }
    return Rotation::Up;
}

inline int manhattanDistance(const Position& lhs, const Position& rhs) noexcept {
    return std::abs(lhs.row - rhs.row) + std::abs(lhs.column - rhs.column);
}

enum class ObjectType {
    Entity,
    Resource,
    Unknown
};

struct InspectInfo {
    int health{0};
    ObjectType type{ObjectType::Unknown};
};
