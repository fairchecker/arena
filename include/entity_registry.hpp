#pragma once

#include <unordered_map>
#include "entity.hpp"

class EntityRegistry {
    public:
    ArenaEntity getEntity(int entityId) {
        return registry_[entityId];
    }

    int addEntity(ArenaEntity entity) {
        registry_[currentNumber_++] = entity;
        return currentNumber_ - 1;
    }
    private:
    int currentNumber_ = 0;
    std::unordered_map<int, ArenaEntity> registry_;
};