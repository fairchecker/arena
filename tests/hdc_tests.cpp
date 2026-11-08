#include "arena.hpp"
#include "hdc_brain.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <random>

class WaitThenEat final : public AgentBrain {
public:
    Action decide(const Observation& observation) override {
        return observation.self.hunger >= 50 ? Action::eat() : Action::wait();
    }
};

class Scripted final : public AgentBrain {
public:
    explicit Scripted(std::vector<Action> actions) : actions_(std::move(actions)) {}
    Action decide(const Observation&) override {
        return index_ < actions_.size() ? actions_[index_++] : Action::wait();
    }
private:
    std::vector<Action> actions_;
    std::size_t index_{0};
};

namespace {
Observation makeObservation(Position position = {2, 2}, int health = 10, int energy = 5,
                            int hunger = 0, int vision = 3) {
    Observation observation;
    observation.self.position = position;
    observation.self.health = observation.self.maxHealth = health;
    observation.self.energy = observation.self.maxEnergy = energy;
    observation.self.hunger = hunger;
    observation.self.maxHunger = 100;
    observation.self.visionRange = observation.self.inspectRange = vision;
    observation.visibleTiles.push_back({position, true, 0});
    return observation;
}

void testOffspringInheritsSafetySummaryAndRestartsMemory() {
    Observation base = makeObservation();
    HdcBrain parent(42);
    // Learn across a spread of situations so multiple clusters form.
    for (int i = 0; i < 40; ++i) {
        auto varied = base;
        varied.self.position = {static_cast<int>((i * 37) % 12), static_cast<int>((i * 53) % 12)};
        varied.self.health = static_cast<int>(1 + (i % 10));
        varied.self.energy = static_cast<int>(1 + (i % 5));
        parent.decide(varied);
    }
    const auto parentStats = parent.diagnostics();
    assert(parentStats.clusters > 1);
    assert(parentStats.transitions > 0);

    auto child = parent.offspring();
    assert(child); // reproducible
    // The child is an HdcBrain with inherited prototypes; per-agent memory restarts.
    auto* childHdc = dynamic_cast<HdcBrain*>(child.get());
    assert(childHdc);
    const auto childStats = childHdc->diagnostics();
    // Selection is bounded, never exceeding MaxInheritedClusters.
    assert(childStats.clusters <= HdcBrain::MaxInheritedClusters);
    assert(childStats.clusters <= parentStats.clusters);
    assert(childStats.transitions == 0);
    assert(childStats.visitedPlaces == 0);

    // The inherited encoding basis (seed) is preserved: the child encodes the same
    // observation to the same hypervector as the parent, so prototypes stay usable.
    assert(childHdc->encode(base) == parent.encode(base));
}

void testOffspringDistillationIsDeterministic() {
    auto buildChild = []() {
        HdcBrain parent(9);
        Observation base = makeObservation();
        std::mt19937 random(17);
        for (int i = 0; i < 100; ++i) {
            auto varied = base;
            varied.self.position = {static_cast<int>(random() % 100U), static_cast<int>(random() % 100U)};
            varied.self.direction = static_cast<Rotation>(random() % 4U);
            varied.self.health = static_cast<int>(random() % 11U);
            varied.self.energy = static_cast<int>(random() % 6U);
            varied.self.hunger = static_cast<int>(random() % 101U);
            varied.visibleTiles.push_back({varied.self.position + Position{static_cast<int>(random() % 9U) - 4,
                static_cast<int>(random() % 9U) - 4}, true, 1});
            parent.decide(varied);
        }
        auto child = parent.offspring();
        auto* childHdc = dynamic_cast<HdcBrain*>(child.get());
        return childHdc->diagnostics();
    };
    const auto firstRun = buildChild();
    const auto secondRun = buildChild();
    assert(firstRun.clusters == secondRun.clusters);
    assert(firstRun.transitions == secondRun.transitions);
    assert(firstRun.clusters > 1);
}

void testNonReproducibleBrainReturnsNullOffspring() {
    HdcBrain brain; // reproduction disabled by default, but offspring() itself is always available
    assert(brain.offspring()); // HDC brains are always reproducible

    // A plain brain (no offspring override) is not reproducible.
    RandomBrain randomBrains(1);
    assert(randomBrains.offspring() == nullptr);
}

void testSurvivalUrgencyPrefersFoodWhenStarvingAndInjured() {
    // Hungry + injured agent on a food tile must eat.
    auto hungryInjured = makeObservation({2, 2}, 4, 5, 90);
    hungryInjured.visibleTiles.push_back({hungryInjured.self.position, true, 1});
    HdcBrain eater;
    assert(eater.decide(hungryInjured).type == ActionType::Eat);

    // Injured agent off food still moves toward visible food.
    auto injured = makeObservation({2, 2}, 4, 5, 60);
    injured.visibleTiles.push_back({{1, 2}, true, 0});
    injured.visibleTiles.push_back({{0, 2}, true, 1});
    injured.self.direction = Rotation::Up;
    HdcBrain mover;
    assert(mover.decide(injured).type == ActionType::Move);
}

void testReproductionDisabledByDefault() {
    // Without an explicit reproduce cost the policy must never choose to reproduce.
    auto ample = makeObservation({2, 2}, 10, 5, 0, 3);
    ample.visibleTiles.push_back({{1, 2}, true, 0});
    ample.visibleTiles.push_back({{2, 3}, true, 0});
    HdcBrain brain;
    for (int i = 0; i < 200; ++i) {
        assert(brain.decide(ample).type != ActionType::Reproduce);
    }
}

void testOffspringSelectionIsBounded() {
    // Force many clusters by visiting widely spread positions (mirrors the proven
    // "growing" setup), then check the child keeps exactly the cap.
    HdcBrain parent(5);
    Observation base = makeObservation();
    std::mt19937 random(123);
    for (int i = 0; i < 500; ++i) {
        auto varied = base;
        varied.self.position = {static_cast<int>(random() % 100U), static_cast<int>(random() % 100U)};
        varied.self.direction = static_cast<Rotation>(random() % 4U);
        varied.self.health = static_cast<int>(random() % 11U);
        varied.self.energy = static_cast<int>(random() % 6U);
        varied.self.hunger = static_cast<int>(random() % 101U);
        varied.visibleTiles.clear();
        varied.visibleTiles.push_back({varied.self.position + Position{static_cast<int>(random() % 9U) - 4,
            static_cast<int>(random() % 9U) - 4}, true, 1});
        varied.visibleEntities = {{varied.self.position + Position{static_cast<int>(random() % 9U) - 4,
            static_cast<int>(random() % 9U) - 4}, Rotation::Up, static_cast<int>(random() % 8U), ObjectType::Entity}};
        parent.decide(varied);
    }
    assert(parent.diagnostics().clusters > HdcBrain::MaxInheritedClusters);

    auto child = parent.offspring();
    auto* childHdc = dynamic_cast<HdcBrain*>(child.get());
    assert(childHdc);
    assert(childHdc->diagnostics().clusters == HdcBrain::MaxInheritedClusters);
}

void testInheritedPriorSurvivesGenerations() {
    // A parent learns a non-neutral safety prior through its own lifetime, then the
    // prior is distilled unchanged through two generations. It must not collapse to
    // neutral (0.5) when a child reproduces, because the prior is carried, not
    // recomputed from the child's (empty) transitions.
    HdcBrain parent(11);
    Observation base = makeObservation();
    std::mt19937 random(31);
    for (int i = 0; i < 200; ++i) {
        auto varied = base;
        varied.self.position = {static_cast<int>(random() % 40U), static_cast<int>(random() % 40U)};
        varied.self.direction = static_cast<Rotation>(random() % 4U);
        varied.self.health = static_cast<int>(random() % 11U);
        varied.self.energy = static_cast<int>(random() % 6U);
        parent.decide(varied);
    }
    const double parentGap = parent.diagnostics().maxPriorAdvantage;
    assert(parentGap > 0.0);

    auto child = parent.offspring();
    auto* childHdc = dynamic_cast<HdcBrain*>(child.get());
    assert(childHdc);
    const double childGap = childHdc->diagnostics().maxPriorAdvantage;
    assert(childGap > 0.0);

    auto grandchild = childHdc->offspring();
    auto* grandchildHdc = dynamic_cast<HdcBrain*>(grandchild.get());
    assert(grandchildHdc);
    const double grandchildGap = grandchildHdc->diagnostics().maxPriorAdvantage;
    assert(grandchildGap > 0.0);
    // The distilled prior value is preserved across generations.
    assert(grandchildGap == childGap);
}

void testInheritedPriorDemonstrablyHelps() {
    // A parent accumulates a non-neutral safety prior through its own decisions;
    // the child inherits that prior, so it holds an actionable safety signal for
    // the situations the parent survived, whereas a fresh brain is neutral. This is
    // learned memory, independent of the hardcoded food heuristic.
    HdcBrain parent(23);
    auto probe = makeObservation({10, 10}, 6, 3, 25, 3);
    probe.visibleTiles.push_back({{9, 10}, true, 0});
    probe.visibleTiles.push_back({{10, 9}, true, 0});
    for (int i = 0; i < 80; ++i) parent.decide(probe);
    assert(parent.diagnostics().maxPriorAdvantage > 0.0);

    auto child = parent.offspring();
    auto* childHdc = dynamic_cast<HdcBrain*>(child.get());
    assert(childHdc);
    assert(childHdc->diagnostics().maxPriorAdvantage > 0.0);

    // A fresh brain with the same seed has no learned prior at all.
    HdcBrain fresh(23);

    const auto childSafety = childHdc->actionSafety(probe);
    const auto freshSafety = fresh.actionSafety(probe);
    // The fresh brain is neutral (no clusters yet): every estimate is exactly 0.5.
    for (std::size_t a = 0; a < HdcBrain::ActionCount; ++a) {
        assert(freshSafety[a] == 0.5);
    }
    // The inherited brain holds at least one non-neutral learned safety estimate.
    bool anyNonNeutral = false;
    for (std::size_t a = 0; a < HdcBrain::ActionCount; ++a) {
        if (childSafety[a] != 0.5) anyNonNeutral = true;
    }
    assert(anyNonNeutral);
}

void testAutonomousReproductionInArena() {
    // A reproduction-enabled HdcBrain population must grow through autonomous birth
    // within the arena, never exceeding the cap, and every descendant must be an
    // HdcBrain (so diagnostics and inheritance continue across generations).
    Arena arena(5, 5);
    arena.setReproductionCost(3);
    arena.setMaxPopulation(4);
    // Add food so the population can survive long enough to reproduce.
    assert(arena.addFoodSource({0, 0}));
    assert(arena.addFood({0, 0}, 4));
    assert(arena.addFoodSource({4, 4}));
    assert(arena.addFood({4, 4}, 4));
    assert(arena.addAgent(ArenaEntity({2, 2}, std::make_unique<HdcBrain>(42, 3), Rotation::Right)) == 0);

    bool reproduced = false;
    for (int tick = 0; tick < 500 && arena.aliveCount() > 0; ++tick) {
        arena.step();
        if (arena.aliveCount() > 1) reproduced = true;
        // The population cap is enforced throughout.
        assert(arena.aliveCount() <= 4);
    }
    assert(reproduced);
    assert(arena.aliveCount() > 0);
    // Every living agent remains an HdcBrain, so diagnostics stay available.
    for (const EntityId id : arena.getEntities().aliveIds()) {
        const ArenaEntity* agent = arena.getAgent(id);
        assert(agent);
        assert(dynamic_cast<const HdcBrain*>(agent->getBrain()) != nullptr);
    }
}
}

int main() {
    Observation observation;
    observation.self.position = {2, 2};
    observation.self.health = observation.self.maxHealth = 10;
    observation.self.energy = observation.self.maxEnergy = 5;
    observation.visibleTiles = {{{2, 2}, true, 1}, {{1, 2}, true, 0}};

    HdcBrain first(42), second(42), different(43);
    const auto encoded = first.encode(observation);
    assert(encoded == second.encode(observation));
    assert(encoded != different.encode(observation));
    assert(HdcBrain::distance(encoded, encoded) == 0.0);
    auto changed = observation;
    changed.self.health = 1;
    assert(HdcBrain::distance(encoded, first.encode(changed)) > 0.0);
    auto elsewhere = observation;
    elsewhere.self.position.column++;
    assert(first.encode(elsewhere) != encoded);

    bool predicted = false, exploredMove = false, exploredRotation = false;
    for (int i = 0; i < 30; ++i) {
        const auto action = first.decide(observation);
        const auto same = second.decide(observation);
        assert(action.type == same.type && action.rotation == same.rotation);
        exploredMove = exploredMove || action.type == ActionType::Move;
        exploredRotation = exploredRotation || action.type == ActionType::Rotate;
        const auto stats = first.diagnostics();
        assert(stats.clusters == 1);
        assert(stats.transitions == static_cast<std::size_t>(i));
        if (stats.predictionError) {
            predicted = true;
            assert(*stats.predictionError == 0.0);
        }
    }
    assert(predicted);
    assert(exploredMove && exploredRotation);
    assert(first.diagnostics().visitedPlaces == 1);
    first.decide(changed);
    assert(first.diagnostics().transitions == 30);
    assert(first.diagnostics().predictionError.has_value());
    assert(*first.diagnostics().predictionError == HdcBrain::distance(encoded, first.encode(changed)));
    first.decide(elsewhere);
    assert(first.diagnostics().visitedPlaces == 2);
    first.decide(observation);
    assert(first.diagnostics().visitedPlaces == 2);

    Arena objects(5, 3);
    const auto id = objects.addAgent(ArenaEntity({1, 1}, std::make_unique<Scripted>(std::vector<Action>{
        Action::inspect({1, 2}), Action::push(), Action::push()}), Rotation::Right));
    assert(objects.addCrate({1, 2}));
    assert(objects.setWall({1, 4}));
    assert(!objects.getWorld().isWalkable({1, 2}));
    assert(objects.step().actions[0].result.success);
    assert(objects.observe(id).lastInspection->info.type == ObjectType::Crate);
    assert(objects.observe(id).lastInspection->info.movable);
    assert(objects.step().actions[0].result.success);
    assert(objects.getAgent(id)->getPosition() == Position({1, 2}));
    assert(objects.getAgent(id)->getEnergy() == 4);
    assert(objects.getWorld().getTile({1, 3}).crate);
    assert(!objects.step().actions[0].result.success);
    assert(objects.getWorld().getTile({1, 3}).crate);
    assert(objects.getAgent(id)->getPosition() == Position({1, 2}));

    Observation crateObservation;
    crateObservation.self.position = {1, 1};
    crateObservation.self.direction = Rotation::Right;
    crateObservation.self.health = crateObservation.self.maxHealth = 10;
    crateObservation.self.energy = crateObservation.self.maxEnergy = 5;
    crateObservation.self.visionRange = crateObservation.self.inspectRange = 3;
    crateObservation.visibleTiles = {{{1, 1}, true, 0}, {{1, 2}, true, 0, true}, {{1, 3}, true, 0}};
    HdcBrain crateBrain;
    assert(crateBrain.decide(crateObservation).type == ActionType::Push);

    Arena renewable(3, 3);
    assert(renewable.addFoodSource({1, 1}));
    assert(renewable.addFood({1, 1}));
    const auto sourceId = renewable.addAgent(ArenaEntity({1, 1},
        std::make_unique<Scripted>(std::vector<Action>{Action::eat()})));
    assert(renewable.step().actions[0].result.success);
    assert(renewable.getWorld().getTile({1, 1}).food == 0);
    renewable.run(48);
    assert(renewable.getWorld().getTile({1, 1}).food == 0);
    renewable.step();
    assert(renewable.getWorld().getTile({1, 1}).food == 1);
    const auto sourceObservation = renewable.observe(sourceId);
    assert(std::any_of(sourceObservation.visibleTiles.begin(), sourceObservation.visibleTiles.end(),
        [](const TileView& tile) { return tile.position == Position{1, 1} && tile.foodSource; }));

    HdcBrain growing(13);
    std::mt19937 random(17);
    for (int i = 0; i < 500; ++i) {
        auto varied = observation;
        varied.self.position = {static_cast<int>(random() % 100U), static_cast<int>(random() % 100U)};
        varied.self.direction = static_cast<Rotation>(random() % 4U);
        varied.self.health = static_cast<int>(random() % 11U);
        varied.self.energy = static_cast<int>(random() % 6U);
        varied.self.hunger = static_cast<int>(random() % 101U);
        varied.visibleTiles.clear();
        varied.visibleTiles.push_back({varied.self.position + Position{static_cast<int>(random() % 9U) - 4,
            static_cast<int>(random() % 9U) - 4}, true, 1});
        varied.visibleEntities = {{varied.self.position + Position{static_cast<int>(random() % 9U) - 4,
            static_cast<int>(random() % 9U) - 4}, Rotation::Up, static_cast<int>(random() % 8U), ObjectType::Entity}};
        growing.decide(varied);
    }
    assert(growing.diagnostics().clusters > 32);
    assert(growing.diagnostics().transitions == 499);

    auto hungry = observation;
    hungry.self.hunger = 90;
    hungry.self.maxHunger = 100;
    HdcBrain eater;
    assert(eater.decide(hungry).type == ActionType::Eat);
    hungry.visibleTiles = {{{2, 2}, true, 0}, {{1, 2}, true, 0}, {{0, 2}, true, 1}};
    hungry.self.visionRange = 3;
    HdcBrain seeker;
    assert(seeker.decide(hungry).type == ActionType::Move);
    hungry.visibleTiles[1].walkable = false;
    HdcBrain blocked;
    assert(blocked.decide(hungry).type != ActionType::Move);

    Arena feeding(3, 3);
    const auto eaterId = feeding.addAgent(ArenaEntity({1, 1}, std::make_unique<WaitThenEat>()));
    assert(feeding.addFood({1, 1}, 1));
    feeding.run(50);
    assert(feeding.getAgent(eaterId)->getHunger() == 50);
    assert(feeding.observe(eaterId).self.hunger == 50);
    feeding.step();
    assert(feeding.getAgent(eaterId)->getHunger() == 11);
    assert(feeding.getWorld().getTile({1, 1}).food == 0);

    Arena starving(2, 2);
    const auto starvingId = starving.addAgent(ArenaEntity({0, 0}));
    starving.run(108);
    assert(starving.getAgent(starvingId)->getHealth() == 10);
    starving.step();
    assert(starving.getAgent(starvingId)->getHealth() == 9);
    starving.run(90);
    assert(starving.aliveCount() == 0);
    assert(!starving.getWorld().isOccupied({0, 0}));

    // Exercise real observations and transitions.
    Arena arena(12, 8);
    auto model = std::make_unique<HdcBrain>(7);
    auto* modelPtr = model.get();
    assert(arena.addAgent(ArenaEntity({2, 2}, std::move(model), Rotation::Right)) >= 0);
    assert(arena.addAgent(ArenaEntity({5, 9}, std::make_unique<RandomBrain>(42), Rotation::Left)) >= 0);
    assert(arena.addFood({1, 1}, 4));
    for (int i = 0; i < 200; ++i) arena.step();
    assert(modelPtr->diagnostics().transitions > 0);
    assert(modelPtr->diagnostics().clusters > 0);

    // Reproduction, inheritance, and survival-behavior tests.
    testOffspringInheritsSafetySummaryAndRestartsMemory();
    testOffspringDistillationIsDeterministic();
    testNonReproducibleBrainReturnsNullOffspring();
    testSurvivalUrgencyPrefersFoodWhenStarvingAndInjured();
    testReproductionDisabledByDefault();
    testOffspringSelectionIsBounded();
    testInheritedPriorSurvivesGenerations();
    testInheritedPriorDemonstrablyHelps();
    testAutonomousReproductionInArena();
    std::cout << "HDC model tests passed\n";
}
