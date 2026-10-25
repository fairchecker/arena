#pragma once

class Entity {
    public:
    int getHealth(){
        return health_;
    }
    void setHealth(int health){
        health_ = health;
    }
    private:
    int id_;
    int health_;
    Position position_;
};

struct Position {
    int row;
    int column;
};