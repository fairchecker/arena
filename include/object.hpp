#pragma once

class Object {
    public:
    ObjectType type_;
    virtual InspectInfo inspect() = 0;
    virtual void takeDamage(int damage) = 0;
};

struct InspectInfo {
    int health;
    ObjectType type;  
};

enum ObjectType {
    Entity,
    Resource,
    Unknown
};