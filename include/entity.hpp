#pragma once

#include "arena.hpp"
#include "object.hpp"

class ArenaEntity : public Object {
    public:
    int getHealth(){
        return health_;
    }

    void setHealth(int health){
        health_ = health;
    }

    InspectInfo inspect(){
        return InspectInfo{health_, type_};
    }

    void takeDamage(int damage) {
        health_ -= damage; // use it freely; registry kills all the agents with <0 hp every tick
    }

    Position getPosition() {
        return position_;
    }

    void setPosition(Position newPosition) {
        position_ = newPosition; // must be checked valified before this method
    }

    Rotation getDirection() {
        return direction_;
    }

    void setDirection(Rotation newDirection) {
        direction_ = newDirection;
    }

    private:
    int id_;
    int health_;
    int damage_;
    int energy_;

    ObjectType type_ = ObjectType::Entity;

    Rotation direction_;
    Position position_;

    int inspectDistance_;
};

