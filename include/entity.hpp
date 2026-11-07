#pragma once

#include "agent.hpp"
#include "object.hpp"

#include <memory>
#include <optional>
#include <utility>

class ArenaEntity final : public Object {
public:
    explicit ArenaEntity(Position position,
                         std::unique_ptr<AgentBrain> brain = {},
                         Rotation direction = Rotation::Up,
                         int health = 10,
                         int damage = 2,
                         int energy = 5,
                         int visionRange = 5,
                         int inspectRange = 3);

    EntityId getId() const noexcept { return id_; }
    void setId(EntityId id) noexcept { id_ = id; }

    int getHealth() const noexcept { return health_; }
    int getMaxHealth() const noexcept { return maxHealth_; }
    void setHealth(int health) noexcept;
    void restoreHealth(int amount) noexcept;

    int getDamage() const noexcept { return damage_; }
    int getEnergy() const noexcept { return energy_; }
    int getMaxEnergy() const noexcept { return maxEnergy_; }
    void restoreEnergy(int amount) noexcept;
    void spendEnergy(int amount) noexcept;

    bool isAlive() const noexcept { return alive_ && health_ > 0; }
    void markDead() noexcept { alive_ = false; }

    InspectInfo inspect() const noexcept override;
    ObjectType objectType() const noexcept override { return ObjectType::Entity; }
    void takeDamage(int damage) noexcept override;

    Position getPosition() const noexcept { return position_; }
    void setPosition(Position position) noexcept { position_ = position; }

    Rotation getDirection() const noexcept { return direction_; }
    void setDirection(Rotation direction) noexcept { direction_ = direction; }

    int getVisionRange() const noexcept { return visionRange_; }
    int getInspectRange() const noexcept { return inspectRange_; }

    Action decide(const Observation& observation);
    void setLastAction(ActionResult result) { lastAction_ = std::move(result); }
    const std::optional<ActionResult>& getLastAction() const noexcept { return lastAction_; }
    void setLastInspection(InspectionResult result) { lastInspection_ = std::move(result); }
    const std::optional<InspectionResult>& getLastInspection() const noexcept { return lastInspection_; }

private:
    EntityId id_{-1};
    int health_;
    int maxHealth_;
    int damage_;
    int energy_;
    int maxEnergy_;
    bool alive_{true};

    ObjectType type_{ObjectType::Entity};
    Rotation direction_;
    Position position_;
    int visionRange_;
    int inspectRange_;
    std::unique_ptr<AgentBrain> brain_;
    std::optional<ActionResult> lastAction_;
    std::optional<InspectionResult> lastInspection_;
};
