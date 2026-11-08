#include "entity_registry.hpp"

#include <algorithm>
#include <limits>
#include <utility>

EntityId EntityRegistry::addEntity(ArenaEntity entity) {
    const Position position = entity.getPosition();
    if (!world_.isWalkable(position) || world_.isOccupied(position) ||
        nextId_ == std::numeric_limits<EntityId>::max()) return -1;

    const EntityId id = nextId_;
    entity.setId(id);
    auto storedEntity = std::make_unique<ArenaEntity>(std::move(entity));
    const auto inserted = entities_.emplace(id, std::move(storedEntity));
    if (!inserted.second) return -1;
    if (!world_.placeEntity(id, position)) {
        entities_.erase(inserted.first);
        return -1;
    }
    ++nextId_;
    return id;
}

ArenaEntity* EntityRegistry::getEntity(EntityId entityId) noexcept {
    const auto it = entities_.find(entityId);
    return it == entities_.end() ? nullptr : it->second.get();
}

const ArenaEntity* EntityRegistry::getEntity(EntityId entityId) const noexcept {
    const auto it = entities_.find(entityId);
    return it == entities_.end() ? nullptr : it->second.get();
}

std::vector<EntityId> EntityRegistry::allIds() const {
    std::vector<EntityId> ids;
    ids.reserve(entities_.size());
    for (const auto& entry : entities_) ids.push_back(entry.first);
    std::sort(ids.begin(), ids.end());
    return ids;
}

bool EntityRegistry::move(EntityId id) {
    ArenaEntity* entity = getEntity(id);
    if (!entity || !entity->isAlive() || entity->getEnergy() <= 0) return false;
    const Position from = entity->getPosition();
    const Position to = from + getFromRotation(entity->getDirection());
    if (!world_.moveEntity(id, from, to)) return false;
    entity->setPosition(to);
    entity->spendEnergy(1);
    return true;
}

bool EntityRegistry::rotate(EntityId id, Rotation newRotation) {
    ArenaEntity* entity = getEntity(id);
    if (!entity || !entity->isAlive() || entity->getDirection() == newRotation) return false;
    entity->setDirection(newRotation);
    return true;
}

bool EntityRegistry::eat(EntityId id) {
    ArenaEntity* entity = getEntity(id);
    if (!entity || !entity->isAlive()) return false;
    const Position position = entity->getPosition();
    if (!world_.consumeFood(position)) return false;
    entity->restoreHealth(2);
    entity->restoreEnergy(2);
    entity->satisfyHunger(40);
    return true;
}

bool EntityRegistry::attack(EntityId id) {
    ArenaEntity* attacker = getEntity(id);
    if (!attacker || !attacker->isAlive() || attacker->getEnergy() <= 0) return false;

    const Position targetPosition = attacker->getPosition() + getFromRotation(attacker->getDirection());
    const auto targetId = world_.entityAt(targetPosition);
    if (!targetId || *targetId == id) return false;

    ArenaEntity* target = getEntity(*targetId);
    if (!target || !target->isAlive()) return false;

    target->takeDamage(attacker->getDamage());
    attacker->spendEnergy(1);
    if (!target->isAlive()) kill(*targetId);
    return true;
}

bool EntityRegistry::kill(EntityId id) {
    ArenaEntity* entity = getEntity(id);
    if (!entity) return false;
    const bool wasAlive = entity->isAlive();
    entity->markDead();
    // takeDamage() may already have changed isAlive() to false; occupancy must
    // still be cleared in that case.
    world_.removeEntity(id, entity->getPosition());
    return wasAlive;
}

std::optional<InspectInfo> EntityRegistry::inspect(EntityId observerId, EntityId targetId) const {
    const ArenaEntity* observer = getEntity(observerId);
    const ArenaEntity* target = getEntity(targetId);
    if (observerId == targetId || !observer || !target ||
        !observer->isAlive() || !target->isAlive()) return std::nullopt;
    if (manhattanDistance(observer->getPosition(), target->getPosition()) > observer->getInspectRange()) {
        return std::nullopt;
    }
    return target->inspect();
}

std::vector<EntityView> EntityRegistry::watch(EntityId observerId) const {
    std::vector<EntityView> result;
    const ArenaEntity* observer = getEntity(observerId);
    if (!observer || !observer->isAlive()) return result;

    for (const auto id : aliveIds()) {
        if (id == observerId) continue;
        const ArenaEntity* candidate = getEntity(id);
        const int distance = manhattanDistance(observer->getPosition(), candidate->getPosition());
        if (distance <= observer->getVisionRange()) {
            result.push_back({candidate->getPosition(), candidate->getDirection(),
                              distance, candidate->objectType()});
        }
    }
    // Do not let internal registry insertion order leak through observation order.
    std::sort(result.begin(), result.end(), [](const EntityView& lhs, const EntityView& rhs) {
        if (lhs.distance != rhs.distance) return lhs.distance < rhs.distance;
        if (lhs.position.row != rhs.position.row) return lhs.position.row < rhs.position.row;
        return lhs.position.column < rhs.position.column;
    });
    return result;
}

void EntityRegistry::checkHealth() {
    for (const auto& entry : entities_) {
        ArenaEntity* entity = entry.second.get();
        if (entity && !entity->isAlive()) {
            world_.removeEntity(entry.first, entity->getPosition());
        }
    }
}

std::vector<EntityId> EntityRegistry::aliveIds() const {
    std::vector<EntityId> result;
    result.reserve(entities_.size());
    for (const auto& entry : entities_) {
        if (entry.second->isAlive()) result.push_back(entry.first);
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::size_t EntityRegistry::aliveCount() const noexcept {
    return static_cast<std::size_t>(std::count_if(entities_.begin(), entities_.end(),
        [](const auto& entry) { return entry.second->isAlive(); }));
}
