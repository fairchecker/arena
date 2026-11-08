#pragma once

#include "agent.hpp"

#include <map>
#include <set>
#include <iosfwd>

// Experimental observed-place graph with resource activation and homeostatic
// modulation. Not a spiking neural network or a learned Q policy.
class GraphBrain final : public AgentBrain {
public:
    struct Diagnostics {
        std::size_t places{0};
        std::size_t sources{0};
        double feedingSignal{0};
        bool feedingMode{false};
        std::size_t propagationRounds{0};
    };
    explicit GraphBrain(bool modulation = true) : modulation_(modulation) {}
    Action decide(const Observation& observation) override;
    Diagnostics diagnostics() const noexcept;
    void forgetResources() noexcept;
    void writeSnapshot(std::ostream& output) const;
    void writeVisits(std::ostream& output) const;

private:
    using Key = std::pair<int, int>;
    struct Place {
        bool traversable{false};
        bool source{false};
        int food{0};
        std::size_t visits{0};
        double activation{0};
    };
    std::map<Key, Place> places_;
    std::set<Key> occupied_;
    bool modulation_{true};
    double feedingSignal_{0};
    bool feedingMode_{false};
    std::size_t rounds_{0};
    std::optional<Position> lastPosition_;
    static Key key(Position position) { return {position.row, position.column}; }
    void propagate(bool resources);
};
