// Task 2 benchmark: does an agent get faster at finding out-of-sight food that always
// sits in the same place?
//
// One "room" per seed: an open room with the food fixed at a random cell and a few
// possible start cells scattered around it at Manhattan distance >= 12 (vision is 5),
// so the food is never visible at the start. Each episode starts the agent at one of the
// start cells with a random facing, hungry, and ends when it eats, dies, or hits the
// horizon. Start cells and facings are drawn from all sides on purpose: a first version
// that always started on the left facing right was solved by "walk straight" and said
// nothing about memory.
//
// For every brain two variants run on exactly the same sequence of episodes:
//   persistent - one brain object lives through all episodes (it may learn);
//   fresh      - a brand-new brain every episode (the memory-reset control).
// A brain that learns shows a falling curve for "persistent" and a flat one for "fresh".
// The optimum (shortest possible time, computed by BFS over position, facing and
// energy) is reported next to every result so that "faster" can be read against
// "as fast as possible".
//
// Usage: memory_curve [--brains hdc,graph,learned,greedy,random] [--seeds N]
//                     [--episodes N] [--block N] [--csv FILE] [--self-test]

#include "arena.hpp"
#include "graph_brain.hpp"
#include "hdc_brain.hpp"
#include "learned_brain.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr int Width = 30;
constexpr int Height = 14;
constexpr int Horizon = 200;       // episode length limit in ticks
constexpr int StartHunger = 60;    // hungry enough that every brain is willing to eat
constexpr int StartEnergy = 5;     // ArenaEntity default
constexpr int StartCells = 4;
constexpr int MinStartDistance = 12;

struct Room {
    Position food{};
    std::array<Position, StartCells> starts{};
};

Room makeRoom(unsigned int seed) {
    std::mt19937 random(seed * 2654435761U + 12345U);
    Room room;
    room.food = {2 + static_cast<int>(random() % 10U), 3 + static_cast<int>(random() % 24U)};
    for (std::size_t i = 0; i < room.starts.size(); ++i) {
        for (;;) {
            const Position candidate{static_cast<int>(random() % static_cast<unsigned int>(Height)),
                                     static_cast<int>(random() % static_cast<unsigned int>(Width))};
            if (manhattanDistance(candidate, room.food) < MinStartDistance) continue;
            if (std::none_of(room.starts.begin(), room.starts.begin() + static_cast<std::ptrdiff_t>(i),
                             [&](const Position& other) { return other == candidate; })) {
                room.starts[i] = candidate;
                break;
            }
        }
    }
    return room;
}

struct Plan {
    Position start;
    Rotation facing;
};

Plan nextPlan(const Room& room, std::mt19937& order) {
    const Position start = room.starts[order() % StartCells];
    return {start, static_cast<Rotation>(order() % 4U)};
}

// Fewest ticks to eat, from a given start and facing with full energy, in an open room.
// Moves cost 1 energy (and need energy > 0), waiting restores 1, eating restores 2,
// and a rotation may face any direction in one tick, exactly as the arena resolves them.
int optimalTicks(Position start, Rotation facing, Position food, int width = Width, int height = Height) {
    struct State { int row, column, direction, energy; };
    const auto index = [&](const State& s) {
        return ((s.row * width + s.column) * 4 + s.direction) * (StartEnergy + 1) + s.energy;
    };
    std::vector<int> distance(static_cast<std::size_t>(width * height * 4 * (StartEnergy + 1)), -1);
    std::queue<State> pending;
    const State initial{start.row, start.column, static_cast<int>(facing), StartEnergy};
    distance[static_cast<std::size_t>(index(initial))] = 0;
    pending.push(initial);
    const std::array<Position, 4> step{{{-1, 0}, {0, 1}, {1, 0}, {0, -1}}}; // Up, Right, Down, Left
    while (!pending.empty()) {
        const State state = pending.front();
        pending.pop();
        const int now = distance[static_cast<std::size_t>(index(state))];
        if (state.row == food.row && state.column == food.column) return now + 1; // the eat tick
        const auto push = [&](const State& next) {
            int& slot = distance[static_cast<std::size_t>(index(next))];
            if (slot < 0) { slot = now + 1; pending.push(next); }
        };
        push({state.row, state.column, state.direction, std::min(StartEnergy, state.energy + 1)}); // wait
        for (int direction = 0; direction < 4; ++direction)
            if (direction != state.direction) push({state.row, state.column, direction, state.energy});
        if (state.energy > 0) {
            const Position delta = step[static_cast<std::size_t>(state.direction)];
            const int row = state.row + delta.row, column = state.column + delta.column;
            if (row >= 0 && row < height && column >= 0 && column < width)
                push({row, column, state.direction, state.energy - 1});
        }
    }
    return -1;
}

// Lets the arena own a thin adapter while the experiment keeps the real brain.
class Borrowed final : public AgentBrain {
public:
    explicit Borrowed(AgentBrain& brain) : brain_(brain) {}
    Action decide(const Observation& observation) override { return brain_.decide(observation); }
    void outcome(const AgentView& state, bool terminal) noexcept override { brain_.outcome(state, terminal); }
private:
    AgentBrain& brain_;
};

// One brain family: how to build it, what to do at episode boundaries.
class Subject {
public:
    virtual ~Subject() = default;
    virtual AgentBrain& brain() = 0;
    virtual void beginEpisode() {}
    // Called after the episode; `arena` still holds the final state.
    virtual void endEpisode(const Arena&, bool /*ate*/) {}
};

class HdcSubject final : public Subject {
public:
    explicit HdcSubject(unsigned int seed) : brain_(seed) {}
    AgentBrain& brain() override { return brain_; }
    void beginEpisode() override { brain_.beginEpisode(); }
    void endEpisode(const Arena& arena, bool ate) override {
        // Present the arriving observation so the delayed TD update of the last
        // action (the meal) is applied, as the existing learning benchmark does.
        if (ate) brain_.decide(arena.observe(0));
    }
private:
    HdcBrain brain_;
};

class LearnedSubject final : public Subject {
public:
    explicit LearnedSubject(unsigned int seed) : brain_(seed) {}
    AgentBrain& brain() override { return brain_; }
    void beginEpisode() override { brain_.beginEpisode(true); }
    void endEpisode(const Arena&, bool) override { brain_.finishEpisode(); }
private:
    LearnedBrain brain_;
};

template <typename Brain>
class PlainSubject final : public Subject {
public:
    explicit PlainSubject(unsigned int seed) : brain_(seed) {}
    AgentBrain& brain() override { return brain_; }
private:
    Brain brain_;
};

class GraphSubject final : public Subject {
public:
    explicit GraphSubject(unsigned int) {}
    AgentBrain& brain() override { return brain_; }
private:
    GraphBrain brain_;
};

std::unique_ptr<Subject> makeSubject(const std::string& name, unsigned int seed) {
    if (name == "hdc") return std::make_unique<HdcSubject>(seed);
    if (name == "learned") return std::make_unique<LearnedSubject>(seed);
    if (name == "graph") return std::make_unique<GraphSubject>(seed);
    if (name == "greedy") return std::make_unique<PlainSubject<GreedyFoodBrain>>(seed);
    if (name == "random") return std::make_unique<PlainSubject<RandomBrain>>(seed);
    return nullptr;
}

struct EpisodeResult {
    int ticks;  // ticks until the meal; Horizon + 1 when it never happened (censored)
    bool ate;
};

EpisodeResult runEpisode(Subject& subject, const Room& world, const Plan& plan) {
    subject.beginEpisode();
    Arena arena(Width, Height);
    arena.addFoodSource(world.food);
    arena.addFood(world.food);
    ArenaEntity entity(plan.start, std::make_unique<Borrowed>(subject.brain()), plan.facing);
    for (int i = 0; i < StartHunger; ++i) entity.advanceHunger();
    arena.addAgent(std::move(entity));
    EpisodeResult result{Horizon + 1, false};
    for (int tick = 1; tick <= Horizon && arena.aliveCount() > 0 && !result.ate; ++tick) {
        for (const auto& record : arena.step().actions)
            if (record.action.type == ActionType::Eat && record.result.success) {
                result = {tick, true};
            }
    }
    subject.endEpisode(arena, result.ate);
    return result;
}

struct Record {
    int seed, episode, optimal, persistentTicks, freshTicks;
    bool persistentAte, freshAte;
};

std::vector<Record> runBrain(const std::string& name, int seeds, int episodes) {
    std::vector<Record> records;
    for (int s = 1; s <= seeds; ++s) {
        const unsigned int seed = static_cast<unsigned int>(s);
        const Room world = makeRoom(seed);
        std::mt19937 order(seed * 40503U + 7U);
        auto persistent = makeSubject(name, seed);
        for (int e = 0; e < episodes; ++e) {
            const Plan plan = nextPlan(world, order);
            const auto learned = runEpisode(*persistent, world, plan);
            auto fresh = makeSubject(name, seed ^ (static_cast<unsigned int>(e + 1) * 2654435761U));
            const auto control = runEpisode(*fresh, world, plan);
            records.push_back({s, e, optimalTicks(plan.start, plan.facing, world.food), learned.ticks, control.ticks,
                               learned.ate, control.ate});
        }
    }
    return records;
}

int median(std::vector<int> values) {
    if (values.empty()) return 0;
    std::sort(values.begin(), values.end());
    return values[values.size() / 2];
}

struct Block { int median; int successPercent; };

Block summarize(const std::vector<Record>& records, int from, int to, bool persistent) {
    std::vector<int> ticks;
    int ok = 0;
    for (const auto& r : records) {
        if (r.episode < from || r.episode >= to) continue;
        ticks.push_back(persistent ? r.persistentTicks : r.freshTicks);
        ok += (persistent ? r.persistentAte : r.freshAte) ? 1 : 0;
    }
    return {median(ticks), ticks.empty() ? 0 : static_cast<int>(100 * ok / static_cast<int>(ticks.size()))};
}

void report(const std::string& name, const std::vector<Record>& records, int episodes, int block) {
    std::cout << name << '\n';
    for (const bool persistent : {true, false}) {
        std::cout << "  " << std::left << std::setw(11) << (persistent ? "persistent" : "fresh") << std::right;
        for (int from = 0; from < episodes; from += block) {
            const Block b = summarize(records, from, std::min(episodes, from + block), persistent);
            std::cout << std::setw(5) << b.median << '/' << std::setw(3) << b.successPercent << '%';
        }
        std::cout << '\n';
    }
    // Paired comparison on the last block: same world, same start cell.
    const int lastFrom = std::max(0, episodes - block);
    int faster = 0, slower = 0, tie = 0;
    for (const auto& r : records) {
        if (r.episode < lastFrom) continue;
        if (r.persistentTicks < r.freshTicks) ++faster;
        else if (r.persistentTicks > r.freshTicks) ++slower;
        else ++tie;
    }
    std::cout << "  last block, persistent vs fresh on identical episodes: faster " << faster
              << ", slower " << slower << ", tie " << tie << '\n';
}

int selfTest() {
    int failures = 0;
    const auto check = [&](bool condition, const char* what) {
        if (!condition) { std::cerr << "self-test failed: " << what << '\n'; ++failures; }
    };
    // Same row, 2 columns away, already facing it: move, move, eat.
    check(optimalTicks({4, 2}, Rotation::Right, {4, 4}) == 3, "straight corridor needs 3 ticks");
    // One row above, facing right: rotate, move, eat.
    check(optimalTicks({4, 2}, Rotation::Right, {3, 2}) == 3, "perpendicular neighbour needs 3 ticks");
    // Six columns away: five moves exhaust the energy, then wait, move, eat.
    check(optimalTicks({4, 2}, Rotation::Right, {4, 8}) == 8, "energy limit forces a wait");
    // Facing away costs exactly one extra rotation.
    check(optimalTicks({4, 2}, Rotation::Left, {4, 4}) == 4, "facing away costs one rotation");
    // A start cell on the food: just eat.
    check(optimalTicks({4, 4}, Rotation::Up, {4, 4}) == 1, "start on food needs 1 tick");

    // The memoryless greedy baseline reaches food it can see, without wasting a tick.
    Room visible;
    visible.food = {4, 6};
    visible.starts.fill({4, 2});
    PlainSubject<GreedyFoodBrain> subject(3);
    check(runEpisode(subject, visible, {{4, 2}, Rotation::Right}).ticks == 5, "greedy walks to visible food in 5 ticks");

    // Determinism: the same configuration reproduces the same records.
    const auto first = runBrain("hdc", 2, 6);
    const auto second = runBrain("hdc", 2, 6);
    bool same = first.size() == second.size();
    for (std::size_t i = 0; same && i < first.size(); ++i)
        same = first[i].persistentTicks == second[i].persistentTicks && first[i].freshTicks == second[i].freshTicks;
    check(same, "benchmark is deterministic");

    // Every world's food is out of sight from every start cell (otherwise there is no memory task).
    for (unsigned int seed = 1; seed <= 50; ++seed) {
        const Room world = makeRoom(seed);
        for (const Position start : world.starts)
            check(manhattanDistance(start, world.food) >= MinStartDistance, "food far and out of sight at start");
    }
    return failures == 0 ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) {
    std::vector<std::string> brains{"hdc", "graph", "learned", "greedy", "random"};
    int seeds = 10, episodes = 60, block = 10;
    std::string csvPath;
    for (int i = 1; i < argc; ++i) {
        const std::string option = argv[i];
        const auto value = [&]() -> std::string {
            if (i + 1 >= argc) { std::cerr << "missing value for " << option << '\n'; std::exit(2); }
            return argv[++i];
        };
        if (option == "--self-test") return selfTest();
        else if (option == "--brains") {
            brains.clear();
            std::stringstream list(value());
            for (std::string item; std::getline(list, item, ',');) brains.push_back(item);
        }
        else if (option == "--seeds") seeds = std::max(1, std::atoi(value().c_str()));
        else if (option == "--episodes") episodes = std::max(1, std::atoi(value().c_str()));
        else if (option == "--block") block = std::max(1, std::atoi(value().c_str()));
        else if (option == "--csv") csvPath = value();
        else { std::cerr << "unknown option " << option << '\n'; return 2; }
    }
    std::ofstream csv;
    if (!csvPath.empty()) {
        std::ifstream existing(csvPath);
        if (existing) { std::cerr << "refusing to overwrite " << csvPath << '\n'; return 2; }
        csv.open(csvPath);
        csv << "brain,seed,episode,optimal,persistent_ticks,persistent_ate,fresh_ticks,fresh_ate\n";
    }
    std::cout << "Task 2: out-of-sight fixed food. Cells show median ticks to eat / % of episodes that ate, per block of "
              << block << " episodes (" << seeds << " worlds, horizon " << Horizon << ", failures count as "
              << Horizon + 1 << ").\n";
    for (const auto& name : brains) {
        if (!makeSubject(name, 1)) { std::cerr << "unknown brain " << name << '\n'; return 2; }
    }
    std::vector<Record> optimum;
    for (int s = 1; s <= seeds; ++s) {
        const Room world = makeRoom(static_cast<unsigned int>(s));
        std::mt19937 order(static_cast<unsigned int>(s) * 40503U + 7U);
        for (int e = 0; e < episodes; ++e) {
            const Plan plan = nextPlan(world, order);
            const int o = optimalTicks(plan.start, plan.facing, world.food);
            optimum.push_back({s, e, o, o, o, true, true});
        }
    }
    std::cout << "optimal    ";
    for (int from = 0; from < episodes; from += block)
        std::cout << std::setw(5) << summarize(optimum, from, std::min(episodes, from + block), true).median << "/100%";
    std::cout << '\n';
    for (const auto& name : brains) {
        const auto records = runBrain(name, seeds, episodes);
        report(name, records, episodes, block);
        if (csv)
            for (const auto& r : records)
                csv << name << ',' << r.seed << ',' << r.episode << ',' << r.optimal << ',' << r.persistentTicks << ','
                    << r.persistentAte << ',' << r.freshTicks << ',' << r.freshAte << '\n';
    }
    return 0;
}
