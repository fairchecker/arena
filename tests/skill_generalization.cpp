#include "arena.hpp"
#include "learned_brain.hpp"

#include <array>
#include <iostream>
#include <memory>
#include <filesystem>
#include <fstream>
#include <string>

class SkillAdapter final : public AgentBrain {
public:
    explicit SkillAdapter(LearnedBrain& brain) : brain_(brain) {}
    Action decide(const Observation& o) override { return brain_.decide(o); }
    void outcome(const AgentView& s, bool terminal) noexcept override { brain_.outcome(s, terminal); }
private:
    LearnedBrain& brain_;
};

int evaluate(LearnedBrain& brain, bool training, int layout, Rotation direction) {
    brain.beginEpisode(training);
    Arena arena(5, 3);
    // 0..2 are training maps; 3 is an unseen composition of two obstacles.
    if (layout == 3) {
        arena.setWall({1, 1});
        arena.setWall({1, 3});
    } else arena.setWall({1, layout + 1});
    arena.addFoodSource({1, 4});
    arena.addFood({1, 4});
    ArenaEntity agent({1, 0}, std::make_unique<SkillAdapter>(brain), direction, 10, 2, 10, 8);
    for (int i = 0; i < 60; ++i) agent.advanceHunger();
    arena.addAgent(std::move(agent));
    for (int tick = 1; tick <= 150 && arena.aliveCount(); ++tick) {
        const auto report = arena.step();
        for (const auto& action : report.actions)
            if (action.action.type == ActionType::Eat && action.result.success) {
                if (training) brain.finishEpisode();
                return tick;
            }
    }
    if (training) brain.finishEpisode();
    return 151;
}

int main(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--train-output") {
        const std::filesystem::path directory(argv[2]);
        std::error_code error;
        if (!std::filesystem::create_directory(directory, error)) {
            std::cerr << "Cannot create new training output directory: " << directory << '\n';
            return 1;
        }
        LearnedBrain trained(42);
        for (int i = 0; i < 3000; ++i)
            evaluate(trained, true, i % 3, static_cast<Rotation>(i % 4));
        std::ofstream file(directory / "skills.txt");
        trained.saveSkills(file);
        file.close();
        if (!file) { std::cerr << "Failed to write skills\n"; return 1; }
        std::cout << "Saved " << trained.diagnostics().states << " skill states to " << directory / "skills.txt" << '\n';
        return 0;
    }
    if (argc != 1) { std::cerr << "Usage: skill_generalization [--train-output NEW_DIRECTORY]\n"; return 1; }
    int totalDiverse = 0, totalFresh = 0;
    for (unsigned int seed : {1U, 7U, 42U}) {
        LearnedBrain single(seed), diverse(seed), fresh(seed);
        for (int i = 0; i < 3000; ++i) {
            const auto direction = static_cast<Rotation>(i % 4);
            evaluate(single, true, 1, direction);
            evaluate(diverse, true, i % 3, direction);
        }
        const auto beforeSingle = single.diagnostics();
        const auto beforeDiverse = diverse.diagnostics();
        int successesSingle = 0, successesDiverse = 0, successesFresh = 0;
        for (int direction = 0; direction < 4; ++direction) {
            const auto facing = static_cast<Rotation>(direction);
            const int one = evaluate(single, false, 3, facing);
            const int multiple = evaluate(diverse, false, 3, facing);
            const int blank = evaluate(fresh, false, 3, facing);
            successesSingle += one <= 150;
            successesDiverse += multiple <= 150;
            successesFresh += blank <= 150;
            std::cout << "{\"seed\":" << seed << ",\"facing\":" << direction
                << ",\"single\":" << one << ",\"diverse\":" << multiple << ",\"fresh\":" << blank << "}\n";
        }
        if (single.diagnostics().updates != beforeSingle.updates || single.diagnostics().states != beforeSingle.states ||
            diverse.diagnostics().updates != beforeDiverse.updates || diverse.diagnostics().states != beforeDiverse.states ||
            fresh.diagnostics().states != 0) return 1;
        std::cerr << "seed " << seed << ": single=" << successesSingle << "/4 diverse=" << successesDiverse
                  << "/4 fresh=" << successesFresh << "/4\n";
        totalDiverse += successesDiverse;
        totalFresh += successesFresh;
    }
    // Narrow deterministic regression check, not a generalization guarantee.
    if (totalDiverse < 9 || totalDiverse <= totalFresh) return 1;
}
