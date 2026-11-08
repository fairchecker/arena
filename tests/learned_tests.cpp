#include "arena.hpp"
#include "learned_brain.hpp"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
#include <memory>
#include <sstream>

class MotorAdapter final : public AgentBrain {
public:
    explicit MotorAdapter(LearnedBrain& brain) : brain_(brain) {}
    Action decide(const Observation& observation) override { return brain_.decide(observation); }
    void outcome(const AgentView& state, bool terminal) noexcept override { brain_.outcome(state, terminal); }
private:
    LearnedBrain& brain_;
};

int episode(LearnedBrain& brain, bool learning, Rotation direction, int offset = 0) {
    brain.beginEpisode(learning);
    Arena arena(5 + offset, 3 + offset);
    // Surround both copies identically; offset layout must not expose extra paths.
    for (int row = 0; row < 3 + offset; ++row)
        for (int column = 0; column < 5 + offset; ++column)
            if (row < offset || column < offset) arena.setWall({row, column});
    arena.setWall({1 + offset, 2 + offset});
    arena.addFoodSource({1 + offset, 4 + offset});
    arena.addFood({1 + offset, 4 + offset});
    ArenaEntity agent({1 + offset, offset}, std::make_unique<MotorAdapter>(brain), direction, 10, 2, 10, 8);
    for (int i = 0; i < 60; ++i) agent.advanceHunger();
    arena.addAgent(std::move(agent));
    for (int tick = 1; tick <= 150 && arena.aliveCount(); ++tick) {
        const auto report = arena.step();
        for (const auto& action : report.actions)
            if (action.action.type == ActionType::Eat && action.result.success) {
                if (learning) brain.finishEpisode();
                return tick;
            }
    }
    return 151;
}

struct SurvivalResult { int meals; bool alive; int ticks; };

SurvivalResult prolongedEpisode(LearnedBrain& brain, bool learning) {
    brain.beginEpisode(learning);
    Arena arena(5, 3);
    arena.setWall({1, 2});
    arena.addFoodSource({1, 4});
    arena.addFood({1, 4});
    ArenaEntity agent({1, 0}, std::make_unique<MotorAdapter>(brain), Rotation::Right, 10, 2, 10, 8);
    for (int i = 0; i < 60; ++i) agent.advanceHunger();
    arena.addAgent(std::move(agent));
    int meals = 0;
    int ticks = 0;
    for (; ticks < 400 && arena.aliveCount(); ++ticks) {
        for (const auto& action : arena.step().actions)
            if (action.action.type == ActionType::Eat && action.result.success) ++meals;
    }
    if (learning) brain.finishEpisode();
    return {meals, arena.aliveCount() != 0, ticks};
}

void testSeed(unsigned int seed) {
    LearnedBrain trained(seed), fresh(seed);
    for (int i = 0; i < 1500; ++i) episode(trained, true, static_cast<Rotation>(i % 4));
    const auto before = trained.diagnostics();
    std::stringstream saved;
    trained.saveSkills(saved);
    LearnedBrain loaded(seed);
    assert(loaded.loadSkills(saved));
    assert(loaded.diagnostics().states == before.states);
    std::stringstream invalid("ARENA_SKILLS 1 1\n1 2 broken\n");
    assert(!loaded.loadSkills(invalid));
    assert(loaded.diagnostics().states == before.states);
    int successes = 0;
    for (int direction = 0; direction < 4; ++direction) {
        const int learned = episode(trained, false, static_cast<Rotation>(direction));
        const int untrained = episode(fresh, false, static_cast<Rotation>(direction));
        const int translated = episode(trained, false, static_cast<Rotation>(direction), 3);
        assert(episode(loaded, false, static_cast<Rotation>(direction)) == learned);
        std::cout << "seed=" << seed << " facing=" << direction << " trained=" << learned << " translated=" << translated << " fresh=" << untrained << '\n';
        if (learned < 151) ++successes;
        assert(translated == learned);
        assert(untrained == 151);
    }
    assert(successes >= 3);
    assert(trained.diagnostics().updates == before.updates);
    assert(trained.diagnostics().states == before.states);
    assert(fresh.diagnostics().states == 0);
    const auto survival = prolongedEpisode(trained, false);
    const auto baseline = prolongedEpisode(fresh, false);
    std::cout << "seed=" << seed << " prolongedMeals=" << survival.meals
              << " alive=" << survival.alive << " freshMeals=" << baseline.meals << '\n';
    assert(survival.alive && survival.ticks == 400 && survival.meals >= 3);
    assert(baseline.meals == 0 && !baseline.alive);
    assert(trained.diagnostics().updates == before.updates && trained.diagnostics().states == before.states);
    assert(fresh.diagnostics().states == 0);
}

int main() {
    for (unsigned int seed : {1U, 7U, 42U}) testSeed(seed);
}
