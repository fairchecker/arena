#include "hdc_brain.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ostream>
#include <iomanip>

namespace {
// Fixed integer mixing, independent of standard-library hash implementations.
std::uint32_t mix(std::uint32_t value) {
    value ^= value >> 16U;
    value *= 0x7feb352dU;
    value ^= value >> 15U;
    value *= 0x846ca68bU;
    return value ^ (value >> 16U);
}

int bucket(int value, int maximum) {
    if (maximum <= 0) return 0;
    const auto bounded = std::clamp(value, 0, maximum);
    return static_cast<int>(static_cast<long long>(bounded) * 4 / maximum);
}

double fraction(int value, int maximum) {
    return maximum > 0 ? static_cast<double>(std::clamp(value, 0, maximum)) / maximum : 0.0;
}

double cost(const AgentView& state) {
    const double hunger = fraction(state.hunger, state.maxHunger);
    const double energyDeficit = 1.0 - fraction(state.energy, state.maxEnergy);
    const double injury = 1.0 - fraction(state.health, state.maxHealth);
    return 2.0 * hunger * hunger * hunger * hunger +
        0.15 * energyDeficit * energyDeficit + 2.0 * injury * injury;
}
}

void HdcBrain::outcome(const AgentView& state, bool terminal) noexcept {
    if (!pending_) return;
    outcomeReward_ = previousCost_ - cost(state) - 0.02 - (terminal ? 5.0 : 0.0);
    if (terminal) {
        auto& cluster = clusters_[pending_->cluster];
        auto& value = cluster.values[pending_->action];
        value += 0.2 * (*outcomeReward_ - value);
        ++cluster.valueUpdates[pending_->action];
        pending_.reset();
        outcomeReward_.reset();
    }
}

void HdcBrain::beginEpisode() noexcept {
    pending_.reset();
    predictionError_.reset();
    outcomeReward_.reset();
    firstTick_.reset();
    lastReproductionAttempt_.reset();
    lastPosition_.reset();
    visits_.clear();
    inspectedCrates_.clear();
    previousCost_ = 0.0;
}

std::array<int, 7> HdcBrain::context(const Observation& observation) {
    const auto& self = observation.self;
    int hereFood = 0, front = 0;
    const Position ahead = self.position + getFromRotation(self.direction);
    for (const auto& tile : observation.visibleTiles) {
        if (tile.position == self.position) hereFood = tile.food > 0 ? 1 : 0;
        if (tile.position == ahead) front = tile.crate ? 3 : (tile.walkable ? 1 : 2);
    }
    for (const auto& entity : observation.visibleEntities)
        if (entity.position == ahead) front = 4;
    return {{bucket(self.hunger, self.maxHunger), bucket(self.health, self.maxHealth),
        bucket(self.energy, self.maxEnergy), static_cast<int>(self.direction), hereFood, front,
        self.energy > 0 ? 1 : 0}};
}

HdcBrain::Hypervector HdcBrain::encode(const Observation& o) const {
    std::array<int, Dimensions> sums{};
    // Binding of a feature role and its discrete value is represented by a
    // deterministic random bipolar vector; bundling is a majority vote.
    auto add = [&](std::uint32_t role, int value) {
        const auto key = mix(seed_ ^ mix(role * 0x9e3779b9U) ^ mix(static_cast<std::uint32_t>(value)));
        for (std::size_t i = 0; i < Dimensions; ++i) {
            sums[i] += (mix(key ^ static_cast<std::uint32_t>(i)) & 1U) ? 1 : -1;
        }
    };
    add(1, bucket(o.self.health, o.self.maxHealth));
    add(2, bucket(o.self.energy, o.self.maxEnergy));
    add(3, static_cast<int>(o.self.direction));
    const Position forward = o.self.position + getFromRotation(o.self.direction);
    int hereFood = 0, front = 0, hereSource = 0;
    int foodDistance = 1000, foodRow = 0, foodColumn = 0;
    for (const auto& tile : o.visibleTiles) {
        if (tile.position == o.self.position) { hereFood = tile.food > 0 ? 1 : 0; hereSource = tile.foodSource ? 1 : 0; }
        if (tile.position == forward) front = tile.crate ? 3 : (tile.walkable ? 1 : 2);
        const int distance = manhattanDistance(o.self.position, tile.position);
        if (tile.food > 0 && distance < foodDistance) {
            foodDistance = distance;
            foodRow = tile.position.row - o.self.position.row;
            foodColumn = tile.position.column - o.self.position.column;
        }
    }
    add(4, hereFood); add(5, front);
    add(6, std::min(foodDistance, 8));
    add(7, std::clamp(foodRow, -8, 8)); add(8, std::clamp(foodColumn, -8, 8));
    int entityDistance = 1000, entityRow = 0, entityColumn = 0;
    for (const auto& entity : o.visibleEntities) {
        if (entity.distance < entityDistance) {
            entityDistance = entity.distance;
            entityRow = entity.position.row - o.self.position.row;
            entityColumn = entity.position.column - o.self.position.column;
        }
    }
    add(9, std::min(entityDistance, 8));
    add(10, std::clamp(entityRow, -8, 8)); add(11, std::clamp(entityColumn, -8, 8));
    // Location is part of the agent's observation, so similar surroundings at
    // different locations can still form distinct memories.
    add(12, o.self.position.row); add(13, o.self.position.column);
    add(14, bucket(o.self.hunger, o.self.maxHunger));
    add(15, o.lastInspection && o.lastInspection->success ? static_cast<int>(o.lastInspection->info.type) + 1 : 0);
    add(16, hereSource);
    add(17, o.lastInspection && o.lastInspection->success && o.lastInspection->info.movable ? 1 : 0);
    Hypervector result{};
    for (std::size_t i = 0; i < Dimensions; ++i) result[i] = sums[i] >= 0 ? 1 : -1;
    return result;
}

double HdcBrain::distance(const Hypervector& a, const Hypervector& b) noexcept {
    std::size_t different = 0;
    for (std::size_t i = 0; i < Dimensions; ++i) if (a[i] != b[i]) ++different;
    return static_cast<double>(different) / static_cast<double>(Dimensions);
}

std::size_t HdcBrain::assign(const Hypervector& vector, const Observation& observation) {
    const auto key = context(observation);
    std::size_t best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < clusters_.size(); ++i) {
        const double d = distance(vector, clusters_[i].prototype);
        if (clusters_[i].context != key) {
            if (d <= 0.20) ++rejectedAliases_;
            continue;
        }
        if (d < bestDistance) { best = i; bestDistance = d; }
    }
    if (clusters_.empty() || bestDistance > 0.20) {
        best = clusters_.size();
        clusters_.emplace_back();
        clusters_.back().quality.fill(0.5);
        clusters_.back().context = key;
    }
    auto& cluster = clusters_[best];
    // Bound prototype counters and adapt gradually during long runs.
    if (cluster.samples >= 1024) {
        for (auto& sum : cluster.sums) sum /= 2;
        cluster.samples /= 2;
    }
    ++cluster.samples;
    for (std::size_t i = 0; i < Dimensions; ++i) {
        cluster.sums[i] += vector[i];
        cluster.prototype[i] = cluster.sums[i] >= 0 ? 1 : -1;
    }
    const double rate = 1.0 / static_cast<double>(cluster.samples);
    cluster.health += (fraction(observation.self.health, observation.self.maxHealth) - cluster.health) * rate;
    cluster.energy += (fraction(observation.self.energy, observation.self.maxEnergy) - cluster.energy) * rate;
    return best;
}

std::unique_ptr<AgentBrain> HdcBrain::offspring() const {
    // Keep the parent's seed so inherited prototypes, which were produced by the
    // parent's deterministic encoding basis, remain interpretable to the child.
    auto child = std::make_unique<HdcBrain>(seed_, reproduceCost_);
    if (clusters_.empty()) return child;

    // Select the most useful clusters: evidence count weighted by expected safety.
    // The per-action prior (`quality`) carries no successor-cluster indices, so it is
    // robust to the renumbering that selection introduces; transitions are never
    // copied, which removes the need for index remapping entirely.
    std::vector<std::size_t> order(clusters_.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const double usefulnessA =
            static_cast<double>(clusters_[a].samples) * (clusters_[a].health + clusters_[a].energy);
        const double usefulnessB =
            static_cast<double>(clusters_[b].samples) * (clusters_[b].health + clusters_[b].energy);
        if (usefulnessA != usefulnessB) return usefulnessA > usefulnessB;
        return a < b; // deterministic tie-break by original index
    });

    const std::size_t keep = std::min(order.size(), MaxInheritedClusters);
    child->clusters_.reserve(keep);
    for (std::size_t i = 0; i < keep; ++i) {
        const auto& source = clusters_[order[i]];
        Cluster distilled;
        distilled.context = source.context;
        distilled.prototype = source.prototype;
        for (std::size_t d = 0; d < Dimensions; ++d) distilled.sums[d] = source.prototype[d];
        distilled.samples = 1;
        distilled.health = source.health;
        distilled.energy = source.energy;
        // The persistent safety prior is carried over unchanged so accumulated
        // experience reaches grandchildren rather than collapsing to neutral.
        distilled.quality = source.quality;
        distilled.values = source.values;
        // priorSamples and transitions are left empty: per-agent memory restarts,
        // but the child begins from the inherited prior value.
        child->clusters_.push_back(std::move(distilled));
    }
    return child;
}

std::optional<std::size_t> HdcBrain::nearestClusterIndex(const Observation& observation) const {
    const auto vector = encode(observation);
    const auto key = context(observation);
    std::size_t best = 0;
    double bestDistance = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < clusters_.size(); ++i) {
        if (clusters_[i].context != key) continue;
        const double d = distance(vector, clusters_[i].prototype);
        if (d < bestDistance) { best = i; bestDistance = d; }
    }
    if (clusters_.empty() || bestDistance > 0.20) return std::nullopt;
    return best;
}

std::array<double, HdcBrain::ActionCount> HdcBrain::actionSafety(const Observation& observation) const {
    std::array<double, ActionCount> result{};
    result.fill(0.5);
    const auto nearest = nearestClusterIndex(observation);
    if (!nearest) return result;
    const auto& cluster = clusters_[*nearest];
    for (std::size_t action = 0; action < ActionCount; ++action) {
        std::size_t total = 0;
        double expected = 0.0;
        for (const auto& entry : cluster.transitions[action]) {
            const std::size_t next = entry.first;
            total += entry.second;
            expected += static_cast<double>(entry.second) *
                (clusters_[next].health + clusters_[next].energy) / 2.0;
        }
        const double local = total ? expected / static_cast<double>(total) : 0.0;
        result[action] = (PriorStrength * cluster.quality[action] +
                          static_cast<double>(total) * local) / (PriorStrength + static_cast<double>(total));
    }
    return result;
}

std::array<double, HdcBrain::ActionCount> HdcBrain::actionValues(const Observation& observation) const {
    const auto nearest = nearestClusterIndex(observation);
    return nearest ? clusters_[*nearest].values : std::array<double, ActionCount>{};
}

bool HdcBrain::hasFreeAdjacent(const Observation& observation) const {
    const Position position = observation.self.position;
    for (const Rotation direction : {Rotation::Up, Rotation::Right, Rotation::Down, Rotation::Left}) {
        const Position candidate = position + getFromRotation(direction);
        const auto tile = std::find_if(observation.visibleTiles.begin(), observation.visibleTiles.end(),
            [&](const TileView& view) { return view.position == candidate; });
        if (tile == observation.visibleTiles.end() || !tile->walkable || tile->crate) continue;
        const bool occupied = std::any_of(observation.visibleEntities.begin(), observation.visibleEntities.end(),
            [&](const EntityView& entity) { return entity.position == candidate; });
        if (!occupied) return true;
    }
    return false;
}

Action HdcBrain::decide(const Observation& observation) {
    if (!firstTick_) firstTick_ = observation.tick;
    if (observation.lastInspection && observation.lastInspection->success &&
        observation.lastInspection->info.type == ObjectType::Crate) {
        const Position target = observation.lastInspection->targetPosition;
        inspectedCrates_.insert({target.row, target.column});
    }
    if (!lastPosition_ || *lastPosition_ != observation.self.position) {
        ++visits_[{observation.self.position.row, observation.self.position.column}];
        lastPosition_ = observation.self.position;
    }
    const auto encoded = encode(observation);
    predictionError_.reset();
    if (pending_ && pending_->prediction) predictionError_ = distance(*pending_->prediction, encoded);
    const std::size_t current = assign(encoded, observation);
    if (pending_) {
        Cluster& source = clusters_[pending_->cluster];
        ++source.transitions[pending_->action][current];
        ++transitionCount_;
        // Update the persistent safety prior as a running mean of arriving-state
        // safety after this action. It is independent of the per-generation
        // transition map, so it survives distillation into offspring.
        const double reward = (clusters_[current].health + clusters_[current].energy) / 2.0;
        const std::size_t n = source.priorSamples[pending_->action]++;
        const double rate = 1.0 / (PriorStrength + static_cast<double>(n) + 1.0);
        source.quality[pending_->action] += rate * (reward - source.quality[pending_->action]);
    }

    // Actions are relative to the current facing; inspection targets only an
    // actually visible entity. Unavailable actions are excluded from planning.
    std::array<Action, ActionCount> actions{{Action::wait(), Action::move(),
        Action::rotate(rotateLeft(observation.self.direction)),
        Action::rotate(rotateRight(observation.self.direction)), Action::eat(),
        Action::attack(), Action::wait(), Action::push(), Action::reproduce()}};
    std::optional<Position> inspectTarget;
    const double hunger = fraction(observation.self.hunger, observation.self.maxHunger);
    const Position position = observation.self.position;
    const Position forward = position + getFromRotation(observation.self.direction);
    const TileView* here = nullptr;
    const TileView* ahead = nullptr;
    const TileView* food = nullptr;
    for (const auto& tile : observation.visibleTiles) {
        if (tile.position == position) here = &tile;
        if (tile.position == forward) ahead = &tile;
        if (tile.crate && manhattanDistance(position, tile.position) <= observation.self.inspectRange &&
            (!inspectTarget || manhattanDistance(position, tile.position) < manhattanDistance(position, *inspectTarget))) inspectTarget = tile.position;
        if (tile.food > 0 && (!food || manhattanDistance(position, tile.position) < manhattanDistance(position, food->position))) food = &tile;
    }
    if (!inspectTarget) {
        for (const auto& entity : observation.visibleEntities) {
            if (entity.distance <= observation.self.inspectRange) { inspectTarget = entity.position; break; }
        }
    }
    if (inspectTarget) actions[6] = Action::inspect(*inspectTarget);
    const Position beyond = forward + getFromRotation(observation.self.direction);
    const auto beyondTile = std::find_if(observation.visibleTiles.begin(), observation.visibleTiles.end(),
        [&](const TileView& tile) { return tile.position == beyond; });
    const bool occupiedBeyond = std::any_of(observation.visibleEntities.begin(), observation.visibleEntities.end(),
        [&](const EntityView& entity) { return entity.position == beyond; });
    const bool occupiedAhead = std::any_of(observation.visibleEntities.begin(), observation.visibleEntities.end(),
        [&](const EntityView& entity) { return entity.position == forward; });
    std::size_t chosen = 0;
    double bestScore = -std::numeric_limits<double>::infinity();
    std::array<bool, ActionCount> available{};
    std::array<double, ActionCount> bonuses{};
    for (std::size_t action = 0; action < ActionCount; ++action) {
        if (action == 6 && !inspectTarget) continue;
        if (action == 1 && (observation.self.energy <= 0 || occupiedAhead ||
            (ahead && (!ahead->walkable || ahead->crate)) || (!ahead && observation.self.visionRange > 0))) continue;
        if (action == 4 && (!here || here->food <= 0 ||
            (hunger < 0.4 && fraction(observation.self.health, observation.self.maxHealth) > 0.5))) continue;
        if (action == 5 && (!occupiedAhead || observation.self.energy <= 0)) continue;
        if (action == 7 && (!ahead || !ahead->crate || observation.self.energy <= 0 ||
            occupiedBeyond || (beyondTile != observation.visibleTiles.end() && (!beyondTile->walkable || beyondTile->crate)) ||
            (beyondTile == observation.visibleTiles.end() && observation.self.visionRange >= 2))) continue;
        if (action == 8 && (reproduceCost_ <= 0 || observation.tick - *firstTick_ < 80 ||
            (lastReproductionAttempt_ && observation.tick - *lastReproductionAttempt_ < 50) ||
            observation.self.energy < reproduceCost_ ||
            hunger >= 0.5 || fraction(observation.self.health, observation.self.maxHealth) < 0.5 ||
             !hasFreeAdjacent(observation))) continue;
        available[action] = true;
        const auto& counts = clusters_[current].transitions[action];
        std::size_t total = 0;
        double expectedPreference = 0;
        double expectedNovelty = 0;
        for (const auto& entry : counts) {
            const std::size_t next = entry.first;
            total += entry.second;
            const double count = static_cast<double>(entry.second);
            expectedPreference += count * (clusters_[next].health + clusters_[next].energy) / 2.0;
            expectedNovelty += count / std::sqrt(static_cast<double>(clusters_[next].samples) + 1.0);
        }
        const double preference = total ? expectedPreference / static_cast<double>(total)
            : (clusters_[current].health + clusters_[current].energy) / 2.0;
        // An untried action may reveal a novel successor. Otherwise use the
        // observed successor distribution to estimate state novelty.
        const double novelty = total ? expectedNovelty / static_cast<double>(total) : 1.0;
        // These count-based exploration bonuses are proxies, not a computed
        // information gain or variational free energy.
        const double uncertainty = 1.0 / std::sqrt(static_cast<double>(total) + 1.0);
        double foodDrive = 0.0;
        if (food) {
            if (action == 4 && food->position == position) foodDrive = 1.0 * hunger;
            if (food->position != position) {
                const int distance = manhattanDistance(position, food->position);
                if (action == 1 && manhattanDistance(forward, food->position) < distance) foodDrive = 0.45 * hunger;
                if (action == 2 || action == 3) {
                    const Position direction = getFromRotation(actions[action].rotation);
                    if (manhattanDistance(position + direction, food->position) < distance) foodDrive = 0.3 * hunger;
                }
            }
        }
        // Survival urgency: a hungry or injured agent needs food soon. This
        // strengthens the food drive above the default secondary preference.
        const double healthFraction = fraction(observation.self.health, observation.self.maxHealth);
        double survival = 1.0;
        if (hunger >= 0.5) survival *= 1.5;
        if (healthFraction <= 0.4) survival *= 1.5;
        foodDrive *= survival;
        // Inherited knowledge: when this action has no locally observed transitions
        // yet, trust the persistent (possibly inherited) safety prior. The prior is
        // carried across generations, so it shapes a child's early decisions rather
        // than collapsing to neutral once the child reproduces.
        double inheritedPrior = 0.0;
        if (total == 0) inheritedPrior = 0.3 * (clusters_[current].quality[action] - 0.5);
        double placeBonus = 0.0;
        if (action == 1 || action == 2 || action == 3 || action == 7) {
            const Rotation direction = (action == 2 || action == 3) ? actions[action].rotation : observation.self.direction;
            const Position target = position + getFromRotation(direction);
            const auto tile = std::find_if(observation.visibleTiles.begin(), observation.visibleTiles.end(),
                [&](const TileView& candidate) { return candidate.position == target; });
            if (tile != observation.visibleTiles.end() && tile->walkable && (!tile->crate || action == 7)) {
                const auto visit = visits_.find({target.row, target.column});
                const std::size_t count = visit == visits_.end() ? 0 : visit->second;
                placeBonus = (action == 1 || action == 7 ? 0.85 : 0.3) / std::sqrt(static_cast<double>(count) + 1.0);
            }
        }
        double actionBias = 0;
        if (action == 2 || action == 3) {
            actionBias -= 0.35;
            if (pending_ && ((pending_->action == 2 && action == 3) || (pending_->action == 3 && action == 2))) actionBias -= 0.2;
        }
        if (action == 0 && observation.self.energy > 1) actionBias -= 0.35;
        // Save up for a possible birth only while well-fed and healthy. The same
        // cooldown also prevents repeated failed attempts when the arena is full.
        if (action == 0 && reproduceCost_ > 0 && observation.tick - *firstTick_ >= 80 &&
            (!lastReproductionAttempt_ || observation.tick - *lastReproductionAttempt_ >= 50) &&
            observation.self.energy < reproduceCost_ && hunger < 0.3 && healthFraction >= 0.5 &&
            hasFreeAdjacent(observation)) actionBias += 2.0;
        if (action == 8) actionBias += 1.0; // make reproduction competitive with exploration
        if (action == 6 && inspectTarget && inspectedCrates_.count({inspectTarget->row, inspectTarget->column}) == 0 &&
            std::any_of(observation.visibleTiles.begin(), observation.visibleTiles.end(), [&](const TileView& tile) {
                return tile.crate && tile.position == *inspectTarget;
            })) actionBias += 0.5;
        // Learned return is the primary preference; curiosity fades with hunger
        // and injury. Food guidance remains a modest prior, not learned evidence.
        const double curiosity = 0.2 + 0.8 * (1.0 - hunger) * (1.0 - hunger) * healthFraction;
        const double score = clusters_[current].values[action] +
            curiosity * (0.3 * uncertainty + 0.15 * novelty + placeBonus) +
            foodDrive + actionBias + 0.05 * inheritedPrior;
        (void)preference;
        bonuses[action] = score - clusters_[current].values[action];
        if (score > bestScore) { bestScore = score; chosen = action; }
    }
    if (pending_) {
        double future = -std::numeric_limits<double>::infinity();
        for (std::size_t a = 0; a < ActionCount; ++a)
            if (available[a]) future = std::max(future, clusters_[current].values[a]);
        auto& source = clusters_[pending_->cluster];
        auto& value = source.values[pending_->action];
        const double reward = outcomeReward_.value_or(previousCost_ - cost(observation.self) - 0.02);
        value += 0.2 * (reward + 0.98 * future - value);
        ++source.valueUpdates[pending_->action];
    }
    bestScore = -std::numeric_limits<double>::infinity();
    for (std::size_t a = 0; a < ActionCount; ++a) {
        const double score = clusters_[current].values[a] + bonuses[a];
        if (available[a] && score > bestScore) { bestScore = score; chosen = a; }
    }
    outcomeReward_.reset();
    previousCost_ = cost(observation.self);
    const auto& counts = clusters_[current].transitions[chosen];
    std::size_t predicted = 0, highest = 0;
    for (const auto& entry : counts) {
        if (entry.second > highest) { highest = entry.second; predicted = entry.first; }
    }
    pending_ = Pending{current, chosen, highest ? std::optional<Hypervector>(clusters_[predicted].prototype) : std::nullopt};
    if (chosen == ActionReproduce) lastReproductionAttempt_ = observation.tick;
    return actions[chosen];
}

HdcBrain::Diagnostics HdcBrain::diagnostics() const noexcept {
    Diagnostics result;
    result.clusters = clusters_.size();
    result.transitions = transitionCount_;
    result.predictionError = predictionError_;
    result.visitedPlaces = visits_.size();
    result.rejectedAliases = rejectedAliases_;
    std::size_t informed = 0;
    double maxGap = 0.0;
    result.memoryLowerBoundBytes = sizeof(*this) + clusters_.capacity() * sizeof(Cluster) +
        visits_.size() * sizeof(decltype(visits_)::value_type) +
        inspectedCrates_.size() * sizeof(decltype(inspectedCrates_)::value_type);
    for (const auto& cluster : clusters_) {
        bool anyInformed = false;
        for (std::size_t action = 0; action < ActionCount; ++action) {
            result.memoryLowerBoundBytes += cluster.transitions[action].size() *
                sizeof(decltype(cluster.transitions)::value_type::value_type);
            const double gap = cluster.quality[action] - 0.5;
            const double magnitude = gap < 0.0 ? -gap : gap;
            if (magnitude > maxGap) maxGap = magnitude;
            if (magnitude > 1e-9) anyInformed = true;
        }
        if (anyInformed) ++informed;
    }
    result.informedClusters = informed;
    result.maxPriorAdvantage = maxGap;
    return result;
}

void HdcBrain::writeSnapshot(std::ostream& out) const {
    out << std::setprecision(17) << "{\"seed\":" << seed_ << ",\"reproduceCost\":" << reproduceCost_
        << ",\"rejectedAliases\":" << rejectedAliases_
        << ",\"previousCost\":" << previousCost_ << ",\"outcomeReward\":";
    if (outcomeReward_) out << *outcomeReward_; else out << "null";
    out
        << ",\"transitionCount\":" << transitionCount_ << ",\"predictionError\":";
    if (predictionError_) out << *predictionError_; else out << "null";
    out << ",\"firstTick\":";
    if (firstTick_) out << *firstTick_; else out << "null";
    out << ",\"lastReproductionAttempt\":";
    if (lastReproductionAttempt_) out << *lastReproductionAttempt_; else out << "null";
    out << ",\"lastPosition\":";
    if (lastPosition_) out << '[' << lastPosition_->row << ',' << lastPosition_->column << ']'; else out << "null";
    out << ",\"pending\":";
    if (pending_) {
        out << "{\"cluster\":" << pending_->cluster << ",\"action\":" << pending_->action << ",\"prediction\":";
        if (pending_->prediction) {
            out << '[';
            for (std::size_t i = 0; i < Dimensions; ++i) { if (i) out << ','; out << static_cast<int>((*pending_->prediction)[i]); }
            out << ']';
        } else out << "null";
        out << '}';
    } else out << "null";
    out << ",\"visits\":[";
    bool first = true;
    for (const auto& v : visits_) { if (!first) out << ','; first = false; out << '[' << v.first.first << ',' << v.first.second << ',' << v.second << ']'; }
    out << "],\"inspectedCrates\":[";
    first = true;
    for (const auto& p : inspectedCrates_) { if (!first) out << ','; first = false; out << '[' << p.first << ',' << p.second << ']'; }
    out << "],\"clusters\":[";
    for (std::size_t i = 0; i < clusters_.size(); ++i) {
        if (i) out << ',';
        const auto& c = clusters_[i];
        out << "{\"context\":[";
        for (std::size_t k = 0; k < c.context.size(); ++k) { if (k) out << ','; out << c.context[k]; }
        out << "],\"samples\":" << c.samples << ",\"health\":" << c.health << ",\"energy\":" << c.energy << ",\"prototype\":[";
        for (std::size_t d = 0; d < Dimensions; ++d) { if (d) out << ','; out << static_cast<int>(c.prototype[d]); }
        out << "],\"sums\":[";
        for (std::size_t d = 0; d < Dimensions; ++d) { if (d) out << ','; out << c.sums[d]; }
        out << "],\"quality\":[";
        for (std::size_t a = 0; a < ActionCount; ++a) { if (a) out << ','; out << c.quality[a]; }
        out << "],\"values\":[";
        for (std::size_t a = 0; a < ActionCount; ++a) { if (a) out << ','; out << c.values[a]; }
        out << "],\"valueUpdates\":[";
        for (std::size_t a = 0; a < ActionCount; ++a) { if (a) out << ','; out << c.valueUpdates[a]; }
        out << "],\"priorSamples\":[";
        for (std::size_t a = 0; a < ActionCount; ++a) { if (a) out << ','; out << c.priorSamples[a]; }
        out << "],\"transitions\":[";
        for (std::size_t a = 0; a < ActionCount; ++a) {
            if (a) out << ',';
            out << '['; first = true;
            for (const auto& entry : c.transitions[a]) { if (!first) out << ','; first = false; out << '[' << entry.first << ',' << entry.second << ']'; }
            out << ']';
        }
        out << "]}";
    }
    out << "]}";
}
