#include "arena.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
class SequenceBrain final : public AgentBrain {
public:
    explicit SequenceBrain(std::vector<Action> actions) : actions_(std::move(actions)) {}
    Action decide(const Observation&) override {
        if (index_ >= actions_.size()) return Action::wait();
        return actions_[index_++];
    }
private:
    std::vector<Action> actions_;
    std::size_t index_{0};
};

void testWorldBoundsAndWalls() {
    World world(4, 3);
    assert(world.inBounds({0, 0}));
    assert(world.inBounds({2, 3}));
    assert(!world.inBounds({3, 0}));
    assert(!world.inBounds({0, 4}));
    assert(world.setWalkable({1, 1}, false));
    assert(!world.isWalkable({1, 1}));
    assert(!world.setWalkable({9, 9}, false));
}

void testMovementAndRotation() {
    Arena arena(5, 5);
    auto brain = std::make_unique<SequenceBrain>(std::vector<Action>{
        Action::move(), Action::rotate(Rotation::Right), Action::move()
    });
    const EntityId id = arena.addAgent(ArenaEntity({2, 2}, std::move(brain), Rotation::Up));
    assert(id == 0);
    assert(arena.step().actions.at(0).result.success);
    assert(arena.getAgent(id)->getPosition() == Position({1, 2}));
    arena.step();
    assert(arena.getAgent(id)->getDirection() == Rotation::Right);
    arena.step();
    assert(arena.getAgent(id)->getPosition() == Position({1, 3}));
}

void testBlockedMovement() {
    Arena arena(3, 3);
    assert(arena.setWall({0, 1}));
    const EntityId id = arena.addAgent(ArenaEntity(
        {1, 1}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::move()}), Rotation::Up));
    assert(id >= 0);
    const auto report = arena.step();
    assert(!report.actions.at(0).result.success);
    assert(arena.getAgent(id)->getPosition() == Position({1, 1}));
}

void testFoodRestoresHealthAndEnergy() {
    Arena arena(3, 3);
    const EntityId eater = arena.addAgent(ArenaEntity(
        {1, 1}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::wait(), Action::eat()}),
        Rotation::Up, 5, 2, 3));
    const EntityId attacker = arena.addAgent(ArenaEntity(
        {1, 2}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::attack(), Action::wait()}),
        Rotation::Left, 10, 2, 3));
    assert(eater == 0 && attacker == 1);
    assert(arena.addFood({1, 1}, 1));
    arena.step(); // The second agent attacks; the first agent waits.
    assert(arena.getAgent(eater)->getHealth() == 3);
    const auto report = arena.step();
    const auto eaterAction = std::find_if(report.actions.begin(), report.actions.end(),
        [eater](const ActionRecord& record) { return record.actorId == eater; });
    assert(eaterAction != report.actions.end());
    assert(eaterAction->result.success);
    assert(arena.getAgent(eater)->getHealth() == 5);
    assert(arena.getAgent(eater)->getEnergy() == 3);
    assert(arena.getWorld().getTile({1, 1}).food == 0);
}

void testAttackAndDeath() {
    Arena arena(3, 3);
    const EntityId attacker = arena.addAgent(ArenaEntity(
        {1, 1}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::attack()}),
        Rotation::Right, 10, 5));
    const EntityId target = arena.addAgent(ArenaEntity(
        {1, 2}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::wait()}),
        Rotation::Left, 4, 1));
    assert(attacker == 0 && target == 1);
    const auto report = arena.step();
    assert(report.actions.at(0).result.success);
    assert(arena.aliveCount() == 1);
    assert(arena.getAgent(target)->getHealth() == 0);
    assert(!arena.getWorld().isOccupied({1, 2}));
    assert(report.aliveAgentsAtStart == 2);
    assert(report.actions.size() == 2);
    assert(!report.actions.at(1).result.success);
    assert(report.actions.at(1).result.message.find("killed before its turn") != std::string::npos);
}

void testWatchAndInspect() {
    Arena arena(8, 3);
    const EntityId observer = arena.addAgent(ArenaEntity(
        {1, 1}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::inspect({1, 3})}),
        Rotation::Right, 10, 2, 5, 4, 3));
    const EntityId target = arena.addAgent(ArenaEntity(
        {1, 3}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::wait()}),
        Rotation::Left, 7));
    assert(target >= 0);
    const auto before = arena.observe(observer);
    assert(before.visibleEntities.size() == 1);
    assert(before.visibleEntities.front().position == Position({1, 3}));
    const auto report = arena.step();
    assert(report.actions.at(0).result.success);
    const auto after = arena.observe(observer);
    assert(after.lastInspection.has_value());
    assert(after.lastInspection->success);
    assert(after.lastInspection->targetPosition == Position({1, 3}));
    assert(after.lastInspection->info.health == 7);
    assert(after.lastInspection->info.type == ObjectType::Entity);
}

void testDeterministicMovementConflict() {
    Arena arena(3, 3);
    const auto first = arena.addAgent(ArenaEntity(
        {1, 0}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::move()}), Rotation::Right));
    const auto second = arena.addAgent(ArenaEntity(
        {1, 2}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::move()}), Rotation::Left));
    assert(first == 0 && second == 1);
    const auto report = arena.step();
    assert(report.actions.size() == 2);
    assert(report.actions[0].actorId == first && report.actions[0].result.success);
    assert(report.actions[1].actorId == second && !report.actions[1].result.success);
    assert(arena.getAgent(first)->getPosition() == Position({1, 1}));
    assert(arena.getAgent(second)->getPosition() == Position({1, 2}));
}

void testInitiativeRotatesBetweenTicks() {
    Arena arena(3, 3);
    const auto first = arena.addAgent(ArenaEntity(
        {0, 0}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::wait(), Action::wait()})));
    const auto second = arena.addAgent(ArenaEntity(
        {2, 2}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::wait(), Action::wait()})));
    assert(first == 0 && second == 1);
    const auto firstTick = arena.step();
    const auto secondTick = arena.step();
    assert(firstTick.actions.at(0).actorId == first);
    assert(secondTick.actions.at(0).actorId == second);
}

void testInspectionUsesWorldPositionNotHiddenEntityId() {
    Arena arena(5, 3);
    // The target (id 0) moves before the observer (id 1) resolves its action.
    // Inspection refers to the observed square, not a hidden persistent entity id.
    arena.addAgent(ArenaEntity(
        {1, 2}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::move()}), Rotation::Right));
    const auto observer = arena.addAgent(ArenaEntity(
        {1, 1}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::inspect({1, 2})}),
        Rotation::Right, 10, 2, 5, 4, 3));
    const auto report = arena.step();
    assert(report.actions.size() == 2);
    assert(report.actions[0].actorId == 0 && report.actions[0].result.success);
    assert(report.actions[1].actorId == observer && !report.actions[1].result.success);
    const auto after = arena.observe(observer);
    assert(after.lastInspection.has_value());
    assert(!after.lastInspection->success);
}

void testWaitRegeneratesEnergy() {
    Arena arena(3, 3);
    const auto id = arena.addAgent(ArenaEntity(
        {1, 1}, std::make_unique<SequenceBrain>(std::vector<Action>{Action::move(), Action::wait()}),
        Rotation::Up, 10, 2, 1));
    const auto moveReport = arena.step();
    assert(moveReport.actions.at(0).result.success);
    assert(arena.getAgent(id)->getEnergy() == 0);
    const auto restReport = arena.step();
    assert(restReport.actions.at(0).result.success);
    assert(arena.getAgent(id)->getEnergy() == 1);
}

void testBrainExceptionDoesNotBreakSimulation() {
    class ThrowingBrain final : public AgentBrain {
    public:
        Action decide(const Observation&) override { throw std::runtime_error("expected test error"); }
    };
    Arena arena(3, 3);
    const auto id = arena.addAgent(ArenaEntity({1, 1}, std::make_unique<ThrowingBrain>()));
    const auto report = arena.step();
    assert(report.actions.size() == 1);
    assert(report.actions.front().actorId == id);
    assert(!report.actions.front().result.success);
    assert(report.actions.front().result.message.find("expected test error") != std::string::npos);
    assert(arena.aliveCount() == 1);
}

void testObservationContainsAgentCapabilities() {
    Arena arena(4, 4);
    const auto id = arena.addAgent(ArenaEntity({2, 2}, {}, Rotation::Left, 13, 4, 9, 6, 2));
    const auto observation = arena.observe(id);
    assert(observation.self.position == Position({2, 2}));
    assert(observation.self.direction == Rotation::Left);
    assert(observation.self.health == 13);
    assert(observation.self.maxHealth == 13);
    assert(observation.self.energy == 9);
    assert(observation.self.maxEnergy == 9);
    assert(observation.self.damage == 4);
    assert(observation.self.visionRange == 6);
    assert(observation.self.inspectRange == 2);
}

void testInvalidRotationIsRejected() {
    Arena arena(3, 3);
    const auto id = arena.addAgent(ArenaEntity(
        {1, 1}, std::make_unique<SequenceBrain>(std::vector<Action>{
            Action::rotate(static_cast<Rotation>(99))
        })));
    const auto report = arena.step();
    assert(id >= 0);
    assert(!report.actions.front().result.success);
    assert(report.actions.front().result.message == "invalid rotation value");

    bool thrown = false;
    try {
        ArenaEntity invalid({0, 0}, {}, static_cast<Rotation>(99));
    } catch (const std::invalid_argument&) {
        thrown = true;
    }
    assert(thrown);
}

void testOverflowGuards() {
    World world(2, 2);
    assert(world.addFood({0, 0}, std::numeric_limits<int>::max()));
    assert(!world.addFood({0, 0}, 1));
    assert(world.getTile({0, 0}).food == std::numeric_limits<int>::max());

    ArenaEntity entity({0, 0}, {}, Rotation::Up, 10, 1, 10);
    entity.setHealth(1);
    entity.restoreHealth(std::numeric_limits<int>::max());
    entity.spendEnergy(10);
    entity.restoreEnergy(std::numeric_limits<int>::max());
    assert(entity.getHealth() == 10);
    assert(entity.getEnergy() == 10);
}

void testSeededRandomBrainsAreReproducible() {
    Arena first(10, 10);
    Arena second(10, 10);
    for (const Position position : {Position{1, 1}, Position{8, 8}, Position{1, 8}, Position{8, 1}}) {
        assert(first.addFood(position, 8));
        assert(second.addFood(position, 8));
    }
    const std::vector<Position> starts{{2, 2}, {7, 7}, {2, 7}, {7, 2}};
    const std::vector<unsigned int> seeds{11U, 22U, 33U, 44U};
    for (std::size_t i = 0; i < starts.size(); ++i) {
        assert(first.addAgent(ArenaEntity(starts[i], std::make_unique<RandomBrain>(seeds[i]))) >= 0);
        assert(second.addAgent(ArenaEntity(starts[i], std::make_unique<RandomBrain>(seeds[i]))) >= 0);
    }

    for (int tick = 0; tick < 100; ++tick) {
        const TickReport left = first.step();
        const TickReport right = second.step();
        assert(left.tick == right.tick);
        assert(left.aliveAgents == right.aliveAgents);
        assert(left.actions.size() == right.actions.size());
        for (std::size_t i = 0; i < left.actions.size(); ++i) {
            assert(left.actions[i].actorId == right.actions[i].actorId);
            assert(left.actions[i].action.type == right.actions[i].action.type);
            assert(left.actions[i].action.rotation == right.actions[i].action.rotation);
            assert(left.actions[i].action.targetPosition == right.actions[i].action.targetPosition);
            assert(left.actions[i].result.success == right.actions[i].result.success);
            assert(left.actions[i].result.message == right.actions[i].result.message);
        }
        for (EntityId id = 0; id < 4; ++id) {
            const ArenaEntity* a = first.getAgent(id);
            const ArenaEntity* b = second.getAgent(id);
            assert(a && b);
            assert(a->isAlive() == b->isAlive());
            assert(a->getPosition() == b->getPosition());
            assert(a->getHealth() == b->getHealth());
            assert(a->getEnergy() == b->getEnergy());
            if (a->isAlive()) {
                assert(first.getWorld().entityAt(a->getPosition()) == id);
                assert(second.getWorld().entityAt(b->getPosition()) == id);
            }
        }
    }
}

void testWorldOccupancyInvariantUnderLongRun() {
    Arena arena(14, 14);
    for (int row = 0; row < 14; ++row) {
        for (int column = 0; column < 14; ++column) {
            if ((row + column) % 5 == 0) assert(arena.addFood({row, column}, 3));
        }
    }
    for (int i = 0; i < 12; ++i) {
        const Position start{1 + (i / 4) * 4, 1 + (i % 4) * 3};
        assert(arena.addAgent(ArenaEntity(
            start, std::make_unique<RandomBrain>(static_cast<unsigned int>(100 + i)))) >= 0);
    }

    arena.run(250);
    std::size_t occupiedCount = 0;
    for (int row = 0; row < 14; ++row) {
        for (int column = 0; column < 14; ++column) {
            const auto occupant = arena.getWorld().entityAt({row, column});
            if (!occupant) continue;
            ++occupiedCount;
            const ArenaEntity* entity = arena.getAgent(*occupant);
            assert(entity && entity->isAlive());
            assert(entity->getPosition() == Position({row, column}));
        }
    }
    assert(occupiedCount == arena.aliveCount());
}

void testInvalidWorldDimensions() {
    bool thrown = false;
    try { World world(0, 4); } catch (const std::invalid_argument&) { thrown = true; }
    assert(thrown);
}
}

int main() {
    testWorldBoundsAndWalls();
    testMovementAndRotation();
    testBlockedMovement();
    testFoodRestoresHealthAndEnergy();
    testAttackAndDeath();
    testWatchAndInspect();
    testDeterministicMovementConflict();
    testInitiativeRotatesBetweenTicks();
    testInspectionUsesWorldPositionNotHiddenEntityId();
    testWaitRegeneratesEnergy();
    testBrainExceptionDoesNotBreakSimulation();
    testObservationContainsAgentCapabilities();
    testInvalidRotationIsRejected();
    testOverflowGuards();
    testSeededRandomBrainsAreReproducible();
    testWorldOccupancyInvariantUnderLongRun();
    testInvalidWorldDimensions();
    std::cout << "All arena tests passed\n";
    return 0;
}
