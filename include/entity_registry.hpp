#pragma once

#include <unordered_map>
#include "entity.hpp"
#include "world.hpp"
#include "types.hpp"

class EntityRegistry {
    public:
    ArenaEntity getEntity(int entityId) {
        return registry_[entityId];
    }

    int addEntity(ArenaEntity entity) {
        registry_[currentNumber_++] = entity;
        return currentNumber_ - 1;
    }

    void eat(int id);
    int attack(int id);
    int move(int id);
    int rotate(int id, Rotation newRotation);

    int kill(int id);

    void watch(int id);
    InspectInfo inspect(int id, int targetId);

    void checkHealth() {
        for(int i = 0; i < currentNumber_; i++){
            if(registry_[i].getHealth() <= 0) { 
                kill(i);
                i--;
            }
        } 
    }

    private:
    int currentNumber_ = 0;
    std::unordered_map<int, ArenaEntity> registry_;
    World world_;
};