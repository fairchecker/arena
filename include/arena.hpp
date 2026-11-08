#pragma once

#include "entity_registry.hpp"
#include "world.hpp"

#include <optional>
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
    bool addCrate(Position position);
    bool addFoodSource(Position position);

    // Asexual reproduction policy. When an agent resolves a `reproduce` action it
    // must hold at least this much energy, and the population (living agents) must
    // be below the cap. The cost is deducted from the parent and the child spawns
    // on the first free adjacent tile in the fixed Up/Right/Down/Left order.
    void setReproductionCost(int cost) noexcept { reproductionCost_ = cost > 0 ? cost : 1; }
    int getReproductionCost() const noexcept { return reproductionCost_; }
    void setMaxPopulation(std::size_t maximum) noexcept { maxPopulation_ = maximum; }
    std::size_t getMaxPopulation() const noexcept { return maxPopulation_; }

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
    std::optional<Position> findSpawnPosition(Position parent) const;

    World world_;
    EntityRegistry entities_;
    unsigned long long tick_{0};
    // Reproduction cost defaults to 3 (more than a move, yet reachable by a default
    // agent whose max energy is 5), leaving the parent viable; the cap bounds growth.
    int reproductionCost_{3};
    std::size_t maxPopulation_{8};
};
