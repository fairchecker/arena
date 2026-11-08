#include "entity.hpp"

#include <algorithm>
#include <stdexcept>
#include <utility>

ArenaEntity::ArenaEntity(Position position,
                         std::unique_ptr<AgentBrain> brain,
                         Rotation direction,
                         int health,
                         int damage,
                         int energy,
                         int visionRange,
                         int inspectRange)
    : health_(health), maxHealth_(health), damage_(damage), energy_(energy),
      maxEnergy_(energy), direction_(direction), position_(position),
      visionRange_(visionRange), inspectRange_(inspectRange), brain_(std::move(brain)) {
    if (health <= 0 || damage < 0 || energy < 0 || visionRange < 0 || inspectRange < 0 ||
        !isValidRotation(direction)) {
        throw std::invalid_argument("Invalid agent configuration");
    }
}

void ArenaEntity::setHealth(int health) noexcept {
    health_ = std::clamp(health, 0, maxHealth_);
    if (health_ == 0) alive_ = false;
}

void ArenaEntity::restoreHealth(int amount) noexcept {
    if (amount > 0 && isAlive()) {
        health_ += std::min(amount, maxHealth_ - health_);
    }
}

void ArenaEntity::restoreEnergy(int amount) noexcept {
    if (amount > 0 && isAlive()) {
        energy_ += std::min(amount, maxEnergy_ - energy_);
    }
}

void ArenaEntity::spendEnergy(int amount) noexcept {
    if (amount > 0) energy_ = std::max(0, energy_ - amount);
}

void ArenaEntity::advanceHunger() noexcept {
    if (!isAlive()) return;
    if (hunger_ < MaxHunger) ++hunger_;
    if (hunger_ == MaxHunger && ++starvingTicks_ >= 10) {
        starvingTicks_ = 0;
        takeDamage(1);
    }
}

void ArenaEntity::satisfyHunger(int amount) noexcept {
    if (amount > 0) {
        hunger_ = std::max(0, hunger_ - amount);
        if (hunger_ < MaxHunger) starvingTicks_ = 0;
    }
}

InspectInfo ArenaEntity::inspect() const noexcept {
    return {health_, type_};
}

void ArenaEntity::takeDamage(int damage) noexcept {
    if (damage <= 0 || !isAlive()) return;
    health_ = std::max(0, health_ - damage);
    if (health_ == 0) alive_ = false;
}

Action ArenaEntity::decide(const Observation& observation) {
    return brain_ ? brain_->decide(observation) : Action::wait();
}

std::unique_ptr<AgentBrain> ArenaEntity::makeOffspringBrain() const {
    return brain_ ? brain_->offspring() : nullptr;
}
