#pragma once

#include "agent.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <iosfwd>
#include <optional>
#include <set>
#include <vector>

// A small, online HDC transition model. Each instance owns its own memory.
class HdcBrain final : public AgentBrain {
public:
    static constexpr std::size_t Dimensions = 256;
    static constexpr std::size_t ActionCount = 9;
    using Hypervector = std::array<std::int8_t, Dimensions>;

    // Indices into the policy's fixed action plan (see decide()).
    static constexpr std::size_t ActionWait = 0;
    static constexpr std::size_t ActionMove = 1;
    static constexpr std::size_t ActionRotateLeft = 2;
    static constexpr std::size_t ActionRotateRight = 3;
    static constexpr std::size_t ActionEat = 4;
    static constexpr std::size_t ActionAttack = 5;
    static constexpr std::size_t ActionInspect = 6;
    static constexpr std::size_t ActionPush = 7;
    static constexpr std::size_t ActionReproduce = 8;

    // Upper bound on the number of clusters distilled into offspring: selection
    // keeps only the most useful (best-evidenced and safest) clusters.
    static constexpr std::size_t MaxInheritedClusters = 16;

    // Strength of the persistent safety prior relative to newly observed evidence
    // when blending learned and inherited experience for action scoring.
    static constexpr double PriorStrength = 2.0;

    struct Diagnostics {
        std::size_t clusters{0};
        std::size_t transitions{0};
        std::optional<double> predictionError; // normalized Hamming distance, 0..1
        std::size_t visitedPlaces{0};
        // Number of clusters whose cartured safety prior differs from neutral (0.5).
        std::size_t informedClusters{0};
        // Largest |quality - 0.5| across all clusters/actions; 0 when naive.
        double maxPriorAdvantage{0.0};
        // Lower bound: object, cluster vector capacity and container values, excluding
        // tree node/allocator overhead and temporary allocations during decisions.
        std::size_t memoryLowerBoundBytes{0};
        std::size_t rejectedAliases{0};
    };

    // `reproduceCost` is this brain's autonomous reproduction threshold: when it is
    // positive the policy may choose to reproduce once energy, health, and hunger
    // conditions are met and a free adjacent tile is visible. The arena still
    // enforces its own global cost and population cap. A non-positive value disables
    // autonomous reproduction (the default), keeping existing consumers unchanged.
    explicit HdcBrain(std::uint32_t seed = 1, int reproduceCost = 0)
        : seed_(seed), reproduceCost_(reproduceCost) {}
    Action decide(const Observation& observation) override;
    void outcome(const AgentView& state, bool terminal) noexcept override;
    // Keep learned knowledge but clear temporal/navigation state between episodes.
    void beginEpisode() noexcept;

    // Distills a bounded, pragmatic subset of learned experience into a child:
    // the top MaxInheritedClusters clusters by evidence-weighted safety, carrying
    // their prototype, safety summary, and the persistent per-action prior. The
    // child keeps this brain's seed so inherited prototypes stay interpretable.
    std::unique_ptr<AgentBrain> offspring() const override;

    Hypervector encode(const Observation& observation) const;
    static double distance(const Hypervector& a, const Hypervector& b) noexcept;
    Diagnostics diagnostics() const noexcept;
    const std::map<std::pair<int, int>, std::size_t>& visits() const noexcept { return visits_; }

    // Read-only safety estimate the policy currently holds for each action in the
    // given observation, blending the persistent (possibly inherited) prior with
    // locally observed transitions. Exposed for tests and GUI diagnostics.
    std::array<double, ActionCount> actionSafety(const Observation& observation) const;
    std::array<double, ActionCount> actionValues(const Observation& observation) const;
    // Diagnostic export only; this is not a checkpoint/restart format.
    void writeSnapshot(std::ostream& output) const;

private:
    struct Cluster {
        // Critical physical/action context must agree before HDC similarity applies.
        std::array<int, 7> context{};
        std::array<int, Dimensions> sums{};
        Hypervector prototype{};
        std::size_t samples{0};
        double health{0};
        double energy{0};
        // Persistent safety prior per action in [0,1]; 0.5 is the neutral default.
        // It is a running mean of arriving-state safety, maintained independently of
        // the per-generation transition map so it survives distillation.
        std::array<double, ActionCount> quality{};
        std::array<double, ActionCount> values{};
        std::array<std::size_t, ActionCount> valueUpdates{};
        // Per-generation count backing the prior mean; resets on distillation so the
        // child's prior starts from the inherited value and adapts by its own data.
        std::array<std::size_t, ActionCount> priorSamples{};
        std::array<std::map<std::size_t, std::size_t>, ActionCount> transitions{};
    };
    struct Pending {
        std::size_t cluster;
        std::size_t action;
        std::optional<Hypervector> prediction;
    };
    std::size_t assign(const Hypervector& vector, const Observation& observation);
    static std::array<int, 7> context(const Observation& observation);
    std::optional<std::size_t> nearestClusterIndex(const Observation& observation) const;
    bool hasFreeAdjacent(const Observation& observation) const;
    std::uint32_t seed_;
    int reproduceCost_{0};
    std::vector<Cluster> clusters_;
    std::optional<Pending> pending_;
    std::optional<double> predictionError_;
    std::size_t transitionCount_{0};
    std::map<std::pair<int, int>, std::size_t> visits_;
    std::optional<Position> lastPosition_;
    std::optional<unsigned long long> firstTick_;
    std::optional<unsigned long long> lastReproductionAttempt_;
    std::set<std::pair<int, int>> inspectedCrates_;
    double previousCost_{0};
    std::optional<double> outcomeReward_;
    std::size_t rejectedAliases_{0};
};
