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
        health_ -= damage;
        if(health_ <= 0) {

        }
    }

    int Move(Position relativePosition);
    int Rotate(Rotation newRotation);
    void Attack();
    void Eat();

    private:
    int id_;
    int health_;
    int damage_;
    int energy_;

    ObjectType type_ = ObjectType::Entity;

    Rotation direction_;
    Position position_;

    std::vector<Object> entityObjects_;
    std::vector<Object> resouceObjects_;

    int inspectDistance_;

    void die() {
        health_ = 0;

    }
};

