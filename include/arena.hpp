#pragma once

#include "entity_registry.hpp"
#include "world.hpp"

#include <vector>

struct ActionRecord {
    EntityId actorId{-1};
    Action action{};
    ActionResult result{};
};

struct TickReport {
    unsigned long long tick{0};
    std::size_t aliveAgentsAtStart{0};
    std::vector<ActionRecord> actions;
    std::size_t aliveAgents{0};
};

class Arena {
public:
    Arena(int width, int height);

    EntityId addAgent(ArenaEntity entity);
    bool addFood(Position position, int amount = 1);
    bool setWall(Position position, bool wall = true);

    Observation observe(EntityId id) const;
    TickReport step();
    std::vector<TickReport> run(unsigned int ticks);

    const World& getWorld() const noexcept { return world_; }
    const EntityRegistry& getEntities() const noexcept { return entities_; }
    const ArenaEntity* getAgent(EntityId id) const noexcept { return entities_.getEntity(id); }
    unsigned long long getTick() const noexcept { return tick_; }
    std::size_t aliveCount() const noexcept { return entities_.aliveCount(); }

private:
    ActionResult resolve(EntityId actorId, const Action& action);
    bool isVisibleTo(EntityId observerId, Position targetPosition) const;

    World world_;
    EntityRegistry entities_;
    unsigned long long tick_{0};
};
