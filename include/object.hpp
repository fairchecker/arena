#pragma once

#include "types.hpp"

class Object {
public:
    virtual ~Object() = default;
    virtual ObjectType objectType() const noexcept = 0;
    virtual InspectInfo inspect() const noexcept = 0;
    virtual void takeDamage(int damage) noexcept = 0;
};
