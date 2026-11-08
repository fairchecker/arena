#pragma once

#include "tile.hpp"
#include "types.hpp"

#include <memory>
#include <optional>
#include <string>
#include <vector>

struct AgentView {
    Position position{};
    Rotation direction{Rotation::Up};
    int health{0};
    int maxHealth{0};
    int energy{0};
    int maxEnergy{0};
    int damage{0};
    int visionRange{0};
    int inspectRange{0};
    int hunger{0};
    int maxHunger{100};
};

// Passive perception deliberately reveals location and orientation, not target health.
struct EntityView {
    Position position{};
    Rotation direction{Rotation::Up};
    int distance{0};
    ObjectType type{ObjectType::Entity};
};

struct ActionResult {
    bool success{false};
    std::string message;
};

struct InspectionResult {
    bool success{false};
    Position targetPosition{};
    InspectInfo info{};
    std::string message;
};

struct Observation {
    unsigned long long tick{0};
    AgentView self{};
    std::vector<TileView> visibleTiles;
    std::vector<EntityView> visibleEntities;
    std::optional<ActionResult> lastAction;
    std::optional<InspectionResult> lastInspection;
};

enum class ActionType {
    Wait,
    Move,
    Rotate,
    Attack,
    Eat,
    Inspect,
    Push,
    Reproduce
};

struct Action {
    ActionType type{ActionType::Wait};
    Rotation rotation{Rotation::Up};
    Position targetPosition{};

    static Action wait() { return {}; }
    static Action move() { return {ActionType::Move, Rotation::Up, {0, 0}}; }
    static Action rotate(Rotation direction) { return {ActionType::Rotate, direction, {0, 0}}; }
    static Action attack() { return {ActionType::Attack, Rotation::Up, {0, 0}}; }
    static Action eat() { return {ActionType::Eat, Rotation::Up, {0, 0}}; }
    static Action inspect(Position target) { return {ActionType::Inspect, Rotation::Up, target}; }
    static Action push() { return {ActionType::Push, Rotation::Up, {0, 0}}; }
    static Action reproduce() { return {ActionType::Reproduce, Rotation::Up, {0, 0}}; }
};

// A brain executes in-process. It receives a read-only snapshot and returns one action.
// It never receives mutable World/Entity references.
class AgentBrain {
public:
    virtual ~AgentBrain() = default;
    virtual Action decide(const Observation& observation) = 0;
    // Called after the full tick, including hunger/combat, even if the agent died.
    virtual void outcome(const AgentView&, bool) noexcept {}

    // Asexual reproduction hook. Returns a freshly constructed child brain seeded
    // with a distilled, bounded subset of this brain's learned experience, or
    // nullptr when the brain cannot reproduce. The default is not reproducible.
    virtual std::unique_ptr<AgentBrain> offspring() const { return nullptr; }
};

class RandomBrain final : public AgentBrain {
public:
    explicit RandomBrain(unsigned int seed = 1);
    Action decide(const Observation& observation) override;

private:
    unsigned int state_;
};

class AggressiveBrain final : public AgentBrain {
public:
    Action decide(const Observation& observation) override;
};

// Memoryless reference policy: when hungry it walks towards the nearest food it can
// currently see (axis-aligned, no pathfinding); otherwise it wanders. It keeps no
// map and learns nothing, so it measures what perception alone achieves. Any agent
// that claims to use memory or learning should beat it on tasks where food is out of
// sight.
class GreedyFoodBrain final : public AgentBrain {
public:
    explicit GreedyFoodBrain(unsigned int seed = 1);
    Action decide(const Observation& observation) override;

private:
    unsigned int next();
    unsigned int state_;
};
