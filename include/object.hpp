#pragma once

#include "types.hpp"

class Object {
    public:
    ObjectType type_;
    virtual InspectInfo inspect() = 0;
    virtual void takeDamage(int damage) = 0;
};

