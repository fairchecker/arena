#include "learned_brain.hpp"

#include <algorithm>
#include <limits>
#include <ostream>
#include <iomanip>
#include <istream>
#include <cmath>
#include <string>

void LearnedBrain::beginEpisode(bool learning) noexcept {
    learning_ = learning;
    pending_.reset();
    reward_.reset();
    resource_.reset();
}

void LearnedBrain::finishEpisode() {
    if (learning_ && pending_ && reward_)
        remember({pending_->state, pending_->action, *reward_, {}, {}, true});
    pending_.reset();
    reward_.reset();
}

void LearnedBrain::saveSkills(std::ostream& out) const {
    out << "ARENA_SKILLS 1 " << values_.size() << '\n' << std::setprecision(17);
    for (const auto& entry : values_) {
        for (int value : entry.first) out << value << ' ';
        for (double value : entry.second) out << value << ' ';
        out << '\n';
    }
}

bool LearnedBrain::loadSkills(std::istream& in) {
    std::string magic;
    unsigned int version = 0;
    std::size_t count = 0;
    if (!(in >> magic >> version >> count) || magic != "ARENA_SKILLS" || version != 1 || count > 1000000) return false;
    std::map<Key, Values> loaded;
    for (std::size_t i = 0; i < count; ++i) {
        Key key{};
        Values values{};
        for (int& value : key) if (!(in >> value)) return false;
        for (double& value : values) if (!(in >> value) || !std::isfinite(value)) return false;
        if (!loaded.emplace(key, values).second) return false;
    }
    in >> std::ws;
    if (!in.eof()) return false;
    values_.swap(loaded);
    replay_ = {};
    replaySize_ = replayCursor_ = updates_ = 0;
    beginEpisode(true);
    return true;
}

void LearnedBrain::writeSnapshot(std::ostream& out) const {
    auto writeKey = [&](const Key& key) {
        out << '[';
        for (std::size_t i = 0; i < key.size(); ++i) { if (i) out << ','; out << key[i]; }
        out << ']';
    };
    out << std::setprecision(17) << "{\"type\":\"learned\",\"learning\":" << (learning_ ? "true" : "false")
        << ",\"updates\":" << updates_ << ",\"randomState\":\"" << random_ << "\",\"resource\":";
    if (resource_) out << '[' << resource_->row << ',' << resource_->column << ']'; else out << "null";
    out << ",\"reward\":";
    if (reward_) out << *reward_; else out << "null";
    out << ",\"pending\":";
    if (pending_) {
        out << "{\"state\":"; writeKey(pending_->state);
        out << ",\"action\":" << pending_->action << ",\"health\":" << pending_->body.health
            << ",\"hunger\":" << pending_->body.hunger << '}';
    } else out << "null";
    out << ",\"states\":[";
    bool first = true;
    for (const auto& entry : values_) {
        if (!first) out << ',';
        first = false;
        out << "{\"key\":"; writeKey(entry.first);
        out << ",\"values\":[";
        for (std::size_t a = 0; a < ActionCount; ++a) { if (a) out << ','; out << entry.second[a]; }
        out << "]}";
    }
    out << "],\"replayCursor\":" << replayCursor_ << ",\"replay\":[";
    for (std::size_t i = 0; i < replaySize_; ++i) {
        if (i) out << ',';
        const auto& transition = *replay_[i];
        out << "{\"state\":"; writeKey(transition.state);
        out << ",\"action\":" << transition.action << ",\"reward\":" << transition.reward << ",\"next\":";
        writeKey(transition.next);
        out << ",\"terminal\":" << (transition.terminal ? "true" : "false") << ",\"feasible\":[";
        for (std::size_t a = 0; a < ActionCount; ++a) { if (a) out << ','; out << (transition.feasible[a] ? "true" : "false"); }
        out << "]}";
    }
    out << "]}";
}

void LearnedBrain::update(const Transition& transition) {
    double future = 0;
    if (!transition.terminal) {
        const auto found = values_.find(transition.next);
        if (found != values_.end()) {
            future = -std::numeric_limits<double>::infinity();
            for (std::size_t a = 0; a < ActionCount; ++a)
                if (transition.feasible[a]) future = std::max(future, found->second[a]);
        }
    }
    auto& value = values_[transition.state][transition.action];
    value += 0.2 * (transition.reward + 0.98 * future - value);
    ++updates_;
}

void LearnedBrain::remember(const Transition& transition) {
    replay_[replayCursor_] = transition;
    replayCursor_ = (replayCursor_ + 1) % replay_.size();
    replaySize_ = std::min(replaySize_ + 1, replay_.size());
    update(transition);
    for (int i = 0; i < 8; ++i) update(*replay_[random_() % replaySize_]);
}

void LearnedBrain::outcome(const AgentView& state, bool terminal) noexcept {
    if (!learning_ || !pending_) return;
    // Task reward, not a distance-to-goal hint: consuming food is detected from
    // the actual reduction of hunger; navigation actions get no directional bonus.
    reward_ = -0.02 + (state.hunger < pending_->body.hunger ? 2.0 : 0.0) -
        0.2 * std::max(0, pending_->body.health - state.health) - (terminal ? 5.0 : 0.0);
    if (terminal) {
        // The source table entry already exists. No allocations inside noexcept.
        auto found = values_.find(pending_->state);
        if (found != values_.end()) {
            auto& value = found->second[pending_->action];
            value += 0.2 * (*reward_ - value);
            ++updates_;
        }
        Transition transition{pending_->state, pending_->action, *reward_, {}, {}, true};
        replay_[replayCursor_] = transition;
        replayCursor_ = (replayCursor_ + 1) % replay_.size();
        replaySize_ = std::min(replaySize_ + 1, replay_.size());
        pending_.reset();
        reward_.reset();
    }
}

Action LearnedBrain::decide(const Observation& observation) {
    const auto& body = observation.self;
    const Position forward = body.position + getFromRotation(body.direction);
    bool foodHere = false, freeAhead = false;
    // Learn resource position from observation only. First known source is a
    // stable context feature, not a waypoint or route imposed on the controller.
    for (const auto& tile : observation.visibleTiles) {
        if (tile.foodSource && !resource_) resource_ = tile.position;
        if (tile.food > 0 && !resource_) resource_ = tile.position;
        if (tile.position == body.position) foodHere = tile.food > 0;
        if (tile.position == forward) freeAhead = tile.walkable && !tile.crate;
    }
    for (const auto& entity : observation.visibleEntities)
        if (entity.position == forward) freeAhead = false;
    const std::array<bool, ActionCount> feasible{{true, freeAhead && body.energy > 0, true, true, foodHere}};
    const Key key{{resource_ ? resource_->row - body.position.row : 0,
        resource_ ? resource_->column - body.position.column : 0, static_cast<int>(body.direction),
        body.energy, body.hunger >= 40 ? 1 : 0, foodHere ? 1 : 0, freeAhead ? 1 : 0,
        resource_ ? 1 : 0}};
    if (learning_) {
        values_.try_emplace(key, Values{});
        if (pending_ && reward_)
            remember({pending_->state, pending_->action, *reward_, key, feasible, false});
    }
    reward_.reset();
    Values values{};
    const auto found = values_.find(key);
    if (found != values_.end()) values = found->second;
    std::size_t chosen = 0;
    if (learning_ && random_() % 100U < 20U) {
        std::array<std::size_t, ActionCount> available{};
        std::size_t count = 0;
        for (std::size_t a = 0; a < ActionCount; ++a) if (feasible[a]) available[count++] = a;
        chosen = available[random_() % count];
    } else {
        double best = -std::numeric_limits<double>::infinity();
        for (std::size_t a = 0; a < ActionCount; ++a)
            if (feasible[a] && values[a] > best) { best = values[a]; chosen = a; }
    }
    if (learning_) pending_ = Pending{key, chosen, body};
    const auto direction = static_cast<int>(body.direction);
    const std::array<Action, ActionCount> actions{{Action::wait(), Action::move(),
        Action::rotate(static_cast<Rotation>((direction + 3) % 4)),
        Action::rotate(static_cast<Rotation>((direction + 1) % 4)), Action::eat()}};
    return actions[chosen];
}
