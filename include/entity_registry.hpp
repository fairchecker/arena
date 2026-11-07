#pragma once

#include "entity.hpp"
#include "world.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

class EntityRegistry {
public:
    explicit EntityRegistry(World& world) : world_(world) {}

    EntityId addEntity(ArenaEntity entity);
    ArenaEntity* getEntity(EntityId entityId) noexcept;
    const ArenaEntity* getEntity(EntityId entityId) const noexcept;

    bool move(EntityId id);
    bool rotate(EntityId id, Rotation newRotation);
    bool eat(EntityId id);
    bool attack(EntityId id);
    bool kill(EntityId id);

    std::optional<InspectInfo> inspect(EntityId observerId, EntityId targetId) const;
    std::vector<EntityView> watch(EntityId observerId) const;

    void checkHealth();
    std::vector<EntityId> aliveIds() const;
    std::size_t size() const noexcept { return entities_.size(); }
    std::size_t aliveCount() const noexcept;

private:
    EntityId nextId_{0};
    World& world_;
    std::unordered_map<EntityId, std::unique_ptr<ArenaEntity>> entities_;
};
