#include "arena.hpp"

#include <algorithm>
#include <cstddef>
#include <exception>
#include <utility>

Arena::Arena(int width, int height) : world_(width, height), entities_(world_) {}

EntityId Arena::addAgent(ArenaEntity entity) {
    return entities_.addEntity(std::move(entity));
}

bool Arena::addFood(Position position, int amount) {
    return world_.addFood(position, amount);
}

bool Arena::setWall(Position position, bool wall) {
    return world_.setWalkable(position, !wall);
}

Observation Arena::observe(EntityId id) const {
    Observation observation;
    const ArenaEntity* self = entities_.getEntity(id);
    if (!self || !self->isAlive()) return observation;

    observation.tick = tick_;
    observation.self = {
        self->getPosition(), self->getDirection(), self->getHealth(), self->getMaxHealth(),
        self->getEnergy(), self->getMaxEnergy(), self->getDamage(),
        self->getVisionRange(), self->getInspectRange()
    };
    observation.visibleEntities = entities_.watch(id);
    observation.lastAction = self->getLastAction();
    observation.lastInspection = self->getLastInspection();

    const Position origin = self->getPosition();
    const int range = self->getVisionRange();
    for (int row = std::max(0, origin.row - range); row <= std::min(world_.getHeight() - 1, origin.row + range); ++row) {
        for (int column = std::max(0, origin.column - range); column <= std::min(world_.getWidth() - 1, origin.column + range); ++column) {
            const Position position{row, column};
            if (manhattanDistance(origin, position) > range) continue;
            const Tile& tile = world_.getTile(position);
            observation.visibleTiles.push_back({position, tile.walkable, tile.food});
        }
    }
    return observation;
}

bool Arena::isVisibleTo(EntityId observerId, Position targetPosition) const {
    const ArenaEntity* observer = entities_.getEntity(observerId);
    const auto targetId = world_.entityAt(targetPosition);
    const ArenaEntity* target = targetId ? entities_.getEntity(*targetId) : nullptr;
    return observer && target && *targetId != observerId && observer->isAlive() && target->isAlive() &&
           manhattanDistance(observer->getPosition(), targetPosition) <= observer->getVisionRange();
}

ActionResult Arena::resolve(EntityId actorId, const Action& action) {
    ArenaEntity* actor = entities_.getEntity(actorId);
    if (!actor || !actor->isAlive()) return {false, "Agent is not alive"};

    switch (action.type) {
        case ActionType::Wait: {
            const int energyBefore = actor->getEnergy();
            actor->restoreEnergy(1);
            const int restored = actor->getEnergy() - energyBefore;
            return {true, restored > 0 ? "rested; restored 1 energy" : "rested; energy already full"};
        }
        case ActionType::Move:
            return entities_.move(actorId)
                ? ActionResult{true, "moved forward"}
                : ActionResult{false, "move blocked or insufficient energy"};
        case ActionType::Rotate:
            if (!isValidRotation(action.rotation)) return {false, "invalid rotation value"};
            return entities_.rotate(actorId, action.rotation)
                ? ActionResult{true, "rotated"}
                : ActionResult{false, "already facing that direction"};
        case ActionType::Attack:
            return entities_.attack(actorId)
                ? ActionResult{true, "attacked the entity ahead"}
                : ActionResult{false, "no attackable entity ahead or insufficient energy"};
        case ActionType::Eat: {
            const int healthBefore = actor->getHealth();
            const int energyBefore = actor->getEnergy();
            if (!entities_.eat(actorId)) return {false, "no food on this tile"};
            const int restoredHealth = actor->getHealth() - healthBefore;
            const int restoredEnergy = actor->getEnergy() - energyBefore;
            return {true, "consumed 1 food; restored " + std::to_string(restoredHealth) +
                " health and " + std::to_string(restoredEnergy) + " energy"};
        }
        case ActionType::Inspect: {
            if (!isVisibleTo(actorId, action.targetPosition)) {
                InspectionResult failed{false, action.targetPosition, {}, "target is not visible"};
                actor->setLastInspection(std::move(failed));
                return {false, "target is not visible"};
            }
            const auto targetId = world_.entityAt(action.targetPosition);
            const auto info = targetId ? entities_.inspect(actorId, *targetId) : std::nullopt;
            if (!info) {
                InspectionResult failed{false, action.targetPosition, {}, "target cannot be inspected"};
                actor->setLastInspection(std::move(failed));
                return {false, "target cannot be inspected"};
            }
            actor->setLastInspection({true, action.targetPosition, *info, "inspection complete"});
            return {true, "inspection complete"};
        }
    }
    return {false, "unknown action"};
}

TickReport Arena::step() {
    // Every decision is made from the same pre-resolution world snapshot.
    // Resolution uses deterministic round-robin initiative to avoid permanently
    // favoring the lowest entity id when actions conflict.
    const auto ids = entities_.aliveIds();
    struct PendingDecision {
        EntityId id;
        Action action;
        std::optional<ActionResult> failure;
    };
    std::vector<PendingDecision> decisions;
    decisions.reserve(ids.size());

    for (const EntityId id : ids) {
        ArenaEntity* entity = entities_.getEntity(id);
        if (!entity || !entity->isAlive()) continue;
        try {
            decisions.push_back({id, entity->decide(observe(id)), std::nullopt});
        } catch (const std::exception& error) {
            decisions.push_back({id, Action::wait(),
                ActionResult{false, std::string("brain exception: ") + error.what()}});
        } catch (...) {
            decisions.push_back({id, Action::wait(),
                ActionResult{false, "brain exception: unknown exception"}});
        }
    }

    if (!decisions.empty()) {
        const auto offset = static_cast<std::ptrdiff_t>(tick_ % decisions.size());
        std::rotate(decisions.begin(), decisions.begin() + offset, decisions.end());
    }

    TickReport report;
    report.tick = tick_ + 1;
    report.aliveAgentsAtStart = decisions.size();
    report.actions.reserve(decisions.size());
    for (const auto& decision : decisions) {
        ArenaEntity* entity = entities_.getEntity(decision.id);
        if (!entity || !entity->isAlive()) {
            report.actions.push_back({decision.id, decision.action,
                {false, "action cancelled: agent was killed before its turn"}});
            continue;
        }
        ActionResult result = decision.failure
            ? *decision.failure
            : resolve(decision.id, decision.action);
        entity->setLastAction(result);
        report.actions.push_back({decision.id, decision.action, std::move(result)});
    }

    entities_.checkHealth();
    ++tick_;
    report.aliveAgents = entities_.aliveCount();
    return report;
}

std::vector<TickReport> Arena::run(unsigned int ticks) {
    std::vector<TickReport> reports;
    reports.reserve(ticks);
    for (unsigned int i = 0; i < ticks && aliveCount() > 0; ++i) {
        reports.push_back(step());
    }
    return reports;
}
