#pragma once

class Entity : public Object {
    public:
    int getHealth(){
        return health_;
    }

    void setHealth(int health){
        health_ = health;
    }

    InspectInfo inspect(){
        return InspectInfo{health_, ObjectType};
    }

    int takeDamage(int damage) {
        health_ -= damage;
        if(health <= 0) {

        }
    }

    void watch() {
        
    }

    int Move(Position relativePosition);
    int Rotate(Rotation newRotation);
    void Attack();

    private:
    int id_;
    int health_;
    int damage_;

    ObjectType type_;

    Rotation direction_;
    Position position_;

    std::vector<Object> entityObjects_;
    std::vector<Object> resouceObjects_;

    int inspectDistance_;

    void die() {
        health_ = 0;

    }
};

struct Position {
    int row;
    int column;
};

enum Rotation {
    Up,
    Right,
    Down,
    Left
}