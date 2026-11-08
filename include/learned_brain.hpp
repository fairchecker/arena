#pragma once

#include "agent.hpp"

#include <array>
#include <map>
#include <random>
#include <iosfwd>

// Learned motor policy. No route search, activation gradient or prescribed turn
// preferences. Discrete actions are the world's motor interface, not a planner.
class LearnedBrain final : public AgentBrain {
public:
    static constexpr std::size_t ActionCount = 5; // wait, move, left, right, eat
    struct Diagnostics {
        std::size_t states{0};
        std::size_t updates{0};
        bool learning{true};
    };
    explicit LearnedBrain(unsigned int seed = 1) : random_(seed) {}
    Action decide(const Observation& observation) override;
    void outcome(const AgentView& state, bool terminal) noexcept override;
    void beginEpisode(bool learning = true) noexcept;
    // End an externally defined episode after the latest outcome. This treats
    // the boundary as terminal but does not invent a death penalty.
    void finishEpisode();
    Diagnostics diagnostics() const noexcept { return {values_.size(), updates_, learning_}; }
    void writeSnapshot(std::ostream& output) const;
    // Portable, versioned policy weights only; excludes replay and episodic state.
    void saveSkills(std::ostream& output) const;
    bool loadSkills(std::istream& input);

private:
    // Stable, translation-invariant relative resource position and local context.
    // This does not encode a route or generalize across rotations by itself.
    using Key = std::array<int, 8>;
    using Values = std::array<double, ActionCount>;
    struct Pending { Key state; std::size_t action; AgentView body; };
    struct Transition { Key state; std::size_t action; double reward; Key next; std::array<bool, ActionCount> feasible; bool terminal; };
    std::map<Key, Values> values_;
    std::array<std::optional<Transition>, 4096> replay_{};
    std::size_t replaySize_{0}, replayCursor_{0}, updates_{0};
    std::optional<Pending> pending_;
    std::optional<double> reward_;
    std::optional<Position> resource_;
    std::mt19937 random_;
    bool learning_{true};
    void update(const Transition& transition);
    void remember(const Transition& transition);
};
