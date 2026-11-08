#include "arena.hpp"
#include "hdc_brain.hpp"

#include <array>
#include <iostream>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <string>

namespace {
void printJsonString(const std::string& text) {
    std::cout << '"';
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') std::cout << '\\' << static_cast<char>(c);
        else if (c < 0x20) {
            std::cout << "\\u00" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c)
                      << std::dec << std::setfill(' ');
        } else std::cout << static_cast<char>(c);
    }
    std::cout << '"';
}

void spawnFood(Arena& arena, std::mt19937& random) {
    const World& world = arena.getWorld();
    for (int attempt = 0; attempt < 16; ++attempt) {
        const int row = static_cast<int>(random() % static_cast<unsigned int>(world.getHeight()));
        const int column = static_cast<int>(random() % static_cast<unsigned int>(world.getWidth()));
        const Position position{row, column};
        const Tile& tile = world.getTile(position);
        if (tile.walkable && !tile.crate && !tile.entityId && tile.food == 0) {
            arena.addFood(position, 1);
            return;
        }
    }
}

std::unique_ptr<Arena> createArena(std::mt19937& random) {
    auto arena = std::make_unique<Arena>(36, 24);
    // Enable autonomous asexual reproduction for the HDC population: agents may be
    // born up to the arena's population cap (default 8) and pay its cost (default 5).
    arena->setReproductionCost(5);
    arena->setMaxPopulation(8);
    for (int row = 0; row < 24; ++row) {
        if (row % 8 != 4) { arena->setWall({row, 12}); arena->setWall({row, 24}); }
    }
    for (int col = 0; col < 36; ++col) {
        if (col % 12 != 6) { arena->setWall({8, col}); arena->setWall({16, col}); }
    }
    const std::array<Position, 3> starts{{{3, 3}, {11, 17}, {20, 31}}};
    for (const Position start : starts) {
        // HdcBrain(seed, reproduceCost): a positive reproduceCost enables autonomous
        // reproduction under safe conditions; the arena still enforces its own cost
        // and cap at resolution time.
        auto brain = std::make_unique<HdcBrain>(1, 5);
        if (arena->addAgent(ArenaEntity(start, std::move(brain), Rotation::Right)) < 0)
            throw std::runtime_error("Could not place GUI agent");
    }
    const std::array<Position, 6> crates{{{2, 5}, {6, 17}, {3, 29}, {12, 5}, {19, 18}, {20, 27}}};
    for (const Position position : crates) {
        if (!arena->addCrate(position)) throw std::runtime_error("Could not place crate");
    }
    const std::array<Position, 5> sources{{{5, 9}, {3, 19}, {13, 28}, {20, 4}, {21, 20}}};
    for (const Position position : sources) {
        if (!arena->addFoodSource(position) || !arena->addFood(position)) throw std::runtime_error("Could not place food source");
    }
    for (int i = 0; i < 8; ++i) spawnFood(*arena, random);
    return arena;
}

const char* actionName(ActionType type) {
    switch (type) {
        case ActionType::Wait: return "wait";
        case ActionType::Move: return "move";
        case ActionType::Rotate: return "rotate";
        case ActionType::Attack: return "attack";
        case ActionType::Eat: return "eat";
        case ActionType::Inspect: return "inspect";
        case ActionType::Push: return "push";
        case ActionType::Reproduce: return "reproduce";
    }
    return "unknown";
}

void printState(const Arena& arena, const TickReport* report) {
    const auto& world = arena.getWorld();
    std::cout << "{\"tick\":" << arena.getTick() << ",\"alive\":" << arena.aliveCount()
              << ",\"width\":" << world.getWidth() << ",\"height\":" << world.getHeight()
              << ",\"tiles\":[";
    bool first = true;
    for (int row = 0; row < world.getHeight(); ++row) {
        for (int col = 0; col < world.getWidth(); ++col) {
            const auto& tile = world.getTile({row, col});
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"wall\":" << (tile.walkable ? "false" : "true")
                      << ",\"food\":" << tile.food << ",\"crate\":" << (tile.crate ? "true" : "false")
                      << ",\"source\":" << (tile.foodSource ? "true" : "false") << '}';
        }
    }
    std::cout << "],\"agents\":[";
    first = true;
    for (EntityId id : arena.getEntities().aliveIds()) {
        const auto* agent = arena.getAgent(id);
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"id\":" << id << ",\"row\":" << agent->getPosition().row
                  << ",\"column\":" << agent->getPosition().column
                  << ",\"direction\":" << static_cast<int>(agent->getDirection())
                  << ",\"health\":" << agent->getHealth() << ",\"maxHealth\":" << agent->getMaxHealth()
                  << ",\"energy\":" << agent->getEnergy() << ",\"maxEnergy\":" << agent->getMaxEnergy()
                  << ",\"hunger\":" << agent->getHunger() << ",\"maxHunger\":" << ArenaEntity::MaxHunger << '}';
    }
    // Models are discovered dynamically from every living agent whose brain is an
    // HdcBrain, so descendants born through reproduction appear automatically.
    std::cout << "],\"models\":[";
    first = true;
    for (EntityId id : arena.getEntities().aliveIds()) {
        const auto* agent = arena.getAgent(id);
        const auto* brain = agent ? dynamic_cast<const HdcBrain*>(agent->getBrain()) : nullptr;
        if (!brain) continue;
        if (!first) std::cout << ',';
        first = false;
        const auto diagnostics = brain->diagnostics();
        std::cout << "{\"id\":" << id << ",\"clusters\":" << diagnostics.clusters
                  << ",\"transitions\":" << diagnostics.transitions << ",\"visited\":" << diagnostics.visitedPlaces
                  << ",\"informedClusters\":" << diagnostics.informedClusters
                  << ",\"maxPriorAdvantage\":" << diagnostics.maxPriorAdvantage
                  << ",\"memoryLowerBoundBytes\":" << diagnostics.memoryLowerBoundBytes
                  << ",\"predictionError\":";
        if (diagnostics.predictionError) std::cout << *diagnostics.predictionError;
        else std::cout << "null";
        std::cout << ",\"visits\":[";
        bool firstVisit = true;
        for (const auto& visit : brain->visits()) {
            if (!firstVisit) std::cout << ',';
            firstVisit = false;
            std::cout << '[' << visit.first.first << ',' << visit.first.second << ',' << visit.second << ']';
        }
        std::cout << "]}";
    }
    std::cout << "],\"actions\":[";
    first = true;
    if (report) {
        for (const auto& action : report->actions) {
            if (!first) std::cout << ',';
            first = false;
            std::cout << "{\"id\":" << action.actorId << ",\"action\":\"" << actionName(action.action.type)
                      << "\",\"success\":" << (action.result.success ? "true" : "false")
                      << ",\"rotation\":" << static_cast<int>(action.action.rotation)
                      << ",\"target\":[" << action.action.targetPosition.row << ',' << action.action.targetPosition.column
                      << "],\"message\":";
            printJsonString(action.result.message);
            std::cout << '}';
        }
    }
    std::cout << "]}" << std::endl;
}

void printSnapshot(const Arena& arena) {
    std::cout << "{\"tick\":" << arena.getTick() << ",\"models\":[";
    bool first = true;
    for (const EntityId id : arena.getEntities().allIds()) {
        const auto* agent = arena.getAgent(id);
        const auto* brain = agent ? dynamic_cast<const HdcBrain*>(agent->getBrain()) : nullptr;
        if (!brain) continue;
        if (!first) std::cout << ',';
        first = false;
        std::cout << "{\"id\":" << id << ",\"alive\":" << (agent->isAlive() ? "true" : "false") << ",\"brain\":";
        brain->writeSnapshot(std::cout);
        std::cout << '}';
    }
    std::cout << "]}" << std::endl;
}
}

int main(int argc, char** argv) {
    std::optional<std::mt19937::result_type> fixedSeed;
    if (argc != 1) {
        if (argc != 3 || std::string(argv[1]) != "--seed") {
            std::cerr << "Usage: arena_gui_backend [--seed 0..4294967295]\n";
            return 1;
        }
        try {
            const std::string value = argv[2];
            std::size_t end = 0;
            const auto seed = std::stoull(value, &end);
            if (value.empty() || value[0] == '-' || end != value.size() ||
                seed > std::numeric_limits<std::uint32_t>::max()) throw std::out_of_range("seed");
            fixedSeed = static_cast<std::mt19937::result_type>(seed);
        } catch (const std::exception&) {
            std::cerr << "Invalid seed\n";
            return 1;
        }
    }
    std::random_device entropy;
    std::mt19937 random(fixedSeed ? *fixedSeed : entropy());
    auto arena = createArena(random);
    std::string command;
    while (std::getline(std::cin, command)) {
        if (command == "step") {
            const auto report = arena->step();
            if (random() % 12U == 0U) spawnFood(*arena, random);
            printState(*arena, &report);
        } else if (command == "reset") {
            random.seed(fixedSeed ? *fixedSeed : entropy());
            arena = createArena(random);
            printState(*arena, nullptr);
        } else if (command == "snapshot") {
            printSnapshot(*arena);
        } else if (command == "state") {
            printState(*arena, nullptr);
        } else {
            std::cout << "{\"error\":\"unknown command\"}" << std::endl;
        }
    }
}
