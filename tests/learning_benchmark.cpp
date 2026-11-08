#include "arena.hpp"
#include "hdc_brain.hpp"

#include <iostream>
#include <memory>

// Arena owns the adapter; the experiment retains the same brain across episodes.
class EpisodeBrain final : public AgentBrain {
public:
    explicit EpisodeBrain(HdcBrain& brain) : brain_(brain) {}
    Action decide(const Observation& observation) override { return brain_.decide(observation); }
    void outcome(const AgentView& state, bool terminal) noexcept override { brain_.outcome(state, terminal); }
private:
    HdcBrain& brain_;
};

int episode(HdcBrain& brain, bool obstacle) {
    brain.beginEpisode();
    Arena arena(5, obstacle ? 3 : 1);
    const int row = obstacle ? 1 : 0;
    arena.addFood({row, 4}, 1);
    if (obstacle) arena.setWall({1, 2});
    ArenaEntity entity({row, 0}, std::make_unique<EpisodeBrain>(brain), Rotation::Right);
    if (!obstacle) for (int i = 0; i < 60; ++i) entity.advanceHunger();
    arena.addAgent(std::move(entity));
    for (int tick = 1; tick <= 200 && arena.aliveCount(); ++tick) {
        const auto report = arena.step();
        for (const auto& action : report.actions)
            if (action.action.type == ActionType::Eat && action.result.success) {
                // Supply the arriving observation so the delayed nonterminal TD
                // update is applied before beginEpisode clears temporal state.
                brain.decide(arena.observe(0));
                return tick;
            }
    }
    return 201; // censored: did not eat within the evaluation horizon
}

int main() {
    for (const bool obstacle : {false, true}) for (unsigned int seed : {1U, 7U, 42U}) {
        HdcBrain trained(seed);
        for (int i = 0; i < 100; ++i) episode(trained, obstacle);
        HdcBrain fresh(seed), reset(seed);
        const int trainedTicks = episode(trained, obstacle);
        const int freshTicks = episode(fresh, obstacle);
        const int resetTicks = episode(reset, obstacle);
        std::cout << "{\"scenario\":\"" << (obstacle ? "wall-detour" : "corridor")
                  << "\",\"seed\":" << seed << ",\"trainedTicksToEat\":" << trainedTicks
                  << ",\"freshTicksToEat\":" << freshTicks << ",\"resetTicksToEat\":" << resetTicks
                  << ",\"clusters\":" << trained.diagnostics().clusters
                  << ",\"rejectedAliases\":" << trained.diagnostics().rejectedAliases << "}\n";
        if (freshTicks != resetTicks) return 1;
        // A narrow reproducible learning regression check, not a claim of general
        // improvement across seeds or the large GUI arena.
        if (obstacle && seed == 42U && trainedTicks >= freshTicks) return 1;
    }
}
