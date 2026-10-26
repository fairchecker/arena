#include "entity_registry.hpp"

int EntityRegistry::move(int id) {
    ArenaEntity entity = registry_[id];
    Position newPosition = entity.getPosition() += getFromRotation(registry_[id].getDirection());
    if(!world_.verifyPosition(newPosition)) return -1;
    entity.setPosition(newPosition);
    return 1;
}

int EntityRegistry::rotate(int id, Rotation rotation) {
    if(rotation == registry_[id].getDirection()) return -1;
    registry_[id].setDirection(rotation);
}