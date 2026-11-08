#include "graph_brain.hpp"

#include <algorithm>
#include <cmath>
#include <ostream>
#include <iomanip>

namespace {
constexpr Rotation Directions[]{Rotation::Up, Rotation::Right, Rotation::Down, Rotation::Left};
double ratio(int value, int maximum) {
    return maximum > 0 ? std::clamp(static_cast<double>(value) / maximum, 0.0, 1.0) : 0.0;
}
}

void GraphBrain::forgetResources() noexcept {
    for (auto& entry : places_) { entry.second.source = false; entry.second.food = 0; }
}

GraphBrain::Diagnostics GraphBrain::diagnostics() const noexcept {
    std::size_t sources = 0;
    for (const auto& entry : places_) if (entry.second.source) ++sources;
    return {places_.size(), sources, feedingSignal_, feedingMode_, rounds_};
}

void GraphBrain::writeVisits(std::ostream& out) const {
    out << '[';
    bool first = true;
    for (const auto& entry : places_) if (entry.second.visits) {
        if (!first) out << ',';
        first = false;
        out << '[' << entry.first.first << ',' << entry.first.second << ',' << entry.second.visits << ']';
    }
    out << ']';
}

void GraphBrain::writeSnapshot(std::ostream& out) const {
    out << std::setprecision(17) << "{\"feedingSignal\":" << feedingSignal_
        << ",\"feedingMode\":" << (feedingMode_ ? "true" : "false")
        << ",\"modulation\":" << (modulation_ ? "true" : "false")
        << ",\"propagationRounds\":" << rounds_ << ",\"lastPosition\":";
    if (lastPosition_) out << '[' << lastPosition_->row << ',' << lastPosition_->column << ']'; else out << "null";
    out << ",\"places\":[";
    bool first = true;
    for (const auto& entry : places_) {
        if (!first) out << ',';
        first = false;
        const auto& node = entry.second;
        out << "{\"position\":[" << entry.first.first << ',' << entry.first.second
            << "],\"traversable\":" << (node.traversable ? "true" : "false")
            << ",\"source\":" << (node.source ? "true" : "false") << ",\"food\":" << node.food
            << ",\"visits\":" << node.visits << ",\"activation\":" << node.activation << '}';
    }
    out << "],\"occupied\":[";
    first = true;
    for (const auto& position : occupied_) {
        if (!first) out << ',';
        first = false;
        out << '[' << position.first << ',' << position.second << ']';
    }
    out << "]}";
}

void GraphBrain::propagate(bool resources) {
    // Max-product messages cannot amplify around cycles. Fixed iteration budget
    // and attenuation bound inference cost and prevent self-excitation.
    for (auto& entry : places_) {
        auto& node = entry.second;
        node.activation = node.traversable && !occupied_.count(entry.first) &&
            (resources ? (node.source || node.food > 0) : node.visits == 0) ? 1.0 : 0.0;
    }
    rounds_ = 0;
    for (std::size_t round = 0; round < std::min<std::size_t>(places_.size(), 256); ++round) {
        std::map<Key, double> next;
        bool changed = false;
        for (const auto& entry : places_) {
            const auto& node = entry.second;
            double value = node.activation;
            if (node.traversable && !occupied_.count(entry.first)) {
                for (Rotation direction : Directions) {
                    const Position neighbor = Position{entry.first.first, entry.first.second} + getFromRotation(direction);
                    const auto found = places_.find(key(neighbor));
                    if (found != places_.end() && found->second.traversable && !occupied_.count(found->first))
                        value = std::max(value, 0.85 * found->second.activation);
                }
            } else value = 0;
            next[entry.first] = value;
            if (std::abs(value - node.activation) > 1e-12) changed = true;
        }
        for (auto& entry : places_) entry.second.activation = next.at(entry.first);
        ++rounds_;
        if (!changed) break;
    }
}

Action GraphBrain::decide(const Observation& observation) {
    occupied_.clear();
    for (const auto& entity : observation.visibleEntities) occupied_.insert(key(entity.position));
    for (const auto& tile : observation.visibleTiles) {
        auto& node = places_[key(tile.position)];
        node.traversable = tile.walkable && !tile.crate;
        node.source = tile.foodSource;
        node.food = tile.food;
    }
    auto& here = places_[key(observation.self.position)];
    here.traversable = true;
    if (!lastPosition_ || *lastPosition_ != observation.self.position) ++here.visits;
    lastPosition_ = observation.self.position;
    const double hunger = ratio(observation.self.hunger, observation.self.maxHunger);
    const double injury = 1.0 - ratio(observation.self.health, observation.self.maxHealth);
    const double target = std::clamp(hunger + 0.25 * injury, 0.0, 1.0);
    feedingSignal_ += (target > feedingSignal_ ? 0.5 : 0.1) * (target - feedingSignal_);
    if (!modulation_) feedingMode_ = false;
    else if (hunger >= 0.55 || feedingSignal_ >= 0.6) feedingMode_ = true;
    else if (hunger < 0.3 && feedingSignal_ < 0.35) feedingMode_ = false;
    if (here.food > 0 && (hunger >= 0.4 || injury >= 0.2)) return Action::eat();
    if (observation.self.energy == 0) return Action::wait();
    // A depleted remembered source remains a goal. Waiting is appropriate only
    // after reaching it, not while repeatedly inspecting unrelated objects.
    if (feedingMode_ && here.source) return Action::wait();
    propagate(feedingMode_);
    auto choose = [&]() -> std::optional<Rotation> {
        std::optional<Rotation> best;
        double activation = 0;
        std::size_t visits = 0;
        // Prefer facing direction for equal values, avoiding unnecessary turns.
        for (int i = 0; i < 4; ++i) {
            const Rotation direction = static_cast<Rotation>((static_cast<int>(observation.self.direction) + i) % 4);
            const auto found = places_.find(key(observation.self.position + getFromRotation(direction)));
            if (found == places_.end() || !found->second.traversable || occupied_.count(found->first)) continue;
            const auto& node = found->second;
            if (!best || node.activation > activation + 1e-12 ||
                (std::abs(node.activation - activation) <= 1e-12 && node.visits < visits)) {
                best = direction; activation = node.activation; visits = node.visits;
            }
        }
        return best;
    };
    // If no resource is reachable in known memory, keep exploring rather than
    // letting a disconnected goal suppress all movement.
    if (feedingMode_ && here.activation == 0) propagate(false);
    const auto direction = choose();
    if (!direction) return Action::wait();
    return *direction == observation.self.direction ? Action::move() : Action::rotate(*direction);
}
