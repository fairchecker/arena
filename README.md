# Arena

A deterministic, in-process environment for experiments with embodied agents. Agents are
ordinary entities in the world. Each entity owns a decision component (`AgentBrain`) that
receives an observation and returns an action. Inference runs locally in the simulation
process: there is no HTTP server, gateway, socket protocol, or remote-agent abstraction.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler (GCC, Clang, or MSVC)

## Build and run

### Windows (PowerShell, Visual Studio or Ninja)

Run these commands from the repository root. The presets select `Release` explicitly
for both the build and CTest, including multi-configuration generators such as Visual
Studio. The demo and test executables are placed in the same predictable directory.

```powershell
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
.\build-release\bin\Release\arena_demo.exe
```

If CMake reports that the build directory was previously configured with a different
generator, remove the old build directory and configure again:

```powershell
Remove-Item -Recurse -Force build-release -ErrorAction SilentlyContinue
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
.\build-release\bin\Release\arena_demo.exe
```

### Linux / macOS

```bash
cmake --preset release
cmake --build --preset release --parallel
ctest --preset release
./build-release/bin/Release/arena_demo
```

The same preset commands work with single-configuration generators. CMake places the
runtime executable under `bin/Release` in either case. CMake 3.21 or newer is required
for presets; alternatively, configure manually with CMake 3.16+:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

To run with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang):

```bash
cmake --preset sanitize
cmake --build --preset sanitize --parallel
ctest --preset sanitize
```

The sanitizer preset is intended for GCC/Clang toolchains. It is not enabled for MSVC.

## Local graphical interface

After building the Release preset, start the browser interface with Python 3:

```bash
python3 gui/server.py
```

Open the printed address if the browser does not open automatically. The interface
shows the actual C++ simulation, with play/pause, single-step, reset and speed controls.
Triangles indicate agent direction, green circles are food, brown squares are
pushable crates, and yellow outlines mark renewable food sources. The optional
visit overlay shows the selected agent's own map; brighter cells were entered more
often. Three independent HDC agents explore a 36x24 arena with nine rooms and
connecting passages. The GUI scenario begins with five food sources (one food
each) and eight randomly placed food items, then has a 1/12 chance per tick to
spawn one food item on a random empty tile. Each reset chooses a new random seed; use
`python3 gui/server.py --seed 42` to reproduce the same run after reset.
Food spawning is specific to the GUI scenario, not an automatic rule of `Arena`.
The server listens only on
`127.0.0.1`; stop it with Ctrl+C. Use `--port 8001` to choose another port, or
`--binary PATH` to use a backend from another build configuration. No Python packages
are required. Set `ARENA_BUILD_GUI=OFF` to omit the GUI backend from the build.

## Long-running experiments

After building the GUI backend, run the same seeded scenario without a browser:

```bash
python3 tools/run_experiment.py --ticks 10000 --seed 42 --snapshot-every 1000 --output experiments/run-42
```

The output directory must not already exist, so previous runs cannot be overwritten.
`--binary PATH` selects another backend build. `metadata.json` records the seed,
parameters and executable SHA-256. `ticks.jsonl` records each tick's actions and
results, living agents' state, scalar model diagnostics and total food. `snapshots.jsonl`
contains the complete HDC model state (including vectors, transition maps, priors,
visits and pending prediction) for every agent, including dead agents, at tick 0,
each requested interval, and the final tick. The run stops early if all agents die.
Snapshots are diagnostic exports, **not** resumable simulation checkpoints; world and
random generator state are not saved. Identical seed and executable produce identical
files, but the JSONL files can become very large, especially with frequent snapshots
and many dead agents. Data is streamed to disk; an interrupted run may be incomplete.

`memoryLowerBoundBytes` in the per-tick model diagnostics estimates a lower bound for
the brain object, cluster capacity and container payloads. It excludes allocator and
tree-node overhead, temporary observation data and the rest of the process; it is not
an RSS measurement. Cluster memory and nearest-cluster search costs grow with learned
experience. The registry also keeps dead agents and their brains for diagnostics.

## Architecture and simulation contract

- **`World`** owns the grid, terrain, food resources, and per-tile occupancy.
- **`ArenaEntity`** is an entity in the world. It owns physical state (position, direction,
  health, energy, damage, perception ranges) and one `AgentBrain` instance.
- **`AgentBrain`** is the only decision interface. It receives a read-only `Observation` and
  returns one `Action`. A brain can own model weights, recurrent state, working memory,
  an HDC/SNN implementation, or a local inference runtime.
- **`Arena`** creates observations, collects decisions, resolves actions, advances ticks,
  and produces reports.
- **`EntityRegistry`** owns entities and keeps the world's occupancy map consistent.

A brain does not receive mutable `World` or `ArenaEntity` references. It cannot directly
teleport, damage, heal, or inspect another entity. To affect the environment it must return
an action, which is validated and applied by the simulation.

### Tick semantics

1. The arena takes the list of living agents in stable entity-ID order.
2. Every living agent decides once using the same pre-resolution world state. Decisions are
   collected before any action is applied.
3. Resolution order rotates by tick to avoid permanently prioritizing the lowest entity ID.
4. Actions are applied sequentially in that order. A move can therefore reserve a tile before
   a later move tries to enter it; an agent killed before its turn has its action marked as
   cancelled in the `TickReport`.
5. Hunger advances, the tick counter advances, food sources refill every 50 ticks,
   and the report records the resulting number of living agents.

This gives models a consistent observation snapshot while retaining deterministic conflict
resolution. The resolution order is part of the environment semantics and should be kept
fixed when comparing model versions.

### Observation and action space

`Observation` contains:

- tick number;
- the agent's position, direction, current and maximum health/energy/hunger, damage, and perception
  ranges;
- visible tiles (walkability, food amount, crate and source presence);
- passively visible entities (position, direction, distance, and object type);
- result of the previous action and the previous active inspection, when available.

Internal entity IDs are deliberately not exposed in observations. An agent can refer to a
visible target by its observed world position, not by a hidden global identifier.

The current action space is intentionally small:

| Action | Effect |
| --- | --- |
| `wait` | Restore 1 energy, up to the agent's maximum. |
| `move` | Move one tile forward if the destination is walkable and unoccupied; costs 1 energy. |
| `rotate(direction)` | Change facing direction. |
| `attack` | Damage the entity on the tile directly ahead; costs 1 energy. |
| `eat` | Consume 1 food on the current tile; restore up to 2 health and 2 energy and reduce hunger by 40. |
| `inspect(position)` | Inspect a visible entity or crate within inspect range. |
| `push` | Push the crate directly ahead one tile and enter its former tile; costs 1 energy. |
| `reproduce` | Spawn an offspring on the first free adjacent tile; costs the arena's reproduction cost. |

Failed moves, attacks and pushes do not consume energy. Rotation and inspection are currently free.
Crates block movement; a push fails if the destination is blocked or occupied.
An empty food source refills to one food every 50 ticks unless covered by a crate.
Each surviving agent gains 1 hunger after actions resolve, up to 100. At maximum
hunger it loses 1 health every 10 consecutive hungry ticks; food interrupts this
counter. Starvation can kill an agent. New agents start at zero hunger.

### Asexual reproduction

A `reproduce` action lets an agent spawn an offspring. Reproduction is a normal
action: it is collected with the other decisions from the same pre-resolution state
and resolved in the rotating initiative order. The offspring is *not* part of the
current tick's decision set, so it first decides on the following tick — this keeps
`TickReport.aliveAgentsAtStart` faithful to the agents that actually decided.

The arena owns a global reproduction policy:

- `reproductionCost` (default 3): the energy deducted from the parent. Reproduction
  fails without spawning if the parent has less energy. Three is a considered value:
  it is several moves' worth of energy, so it is a real investment, yet it is below a
  default agent's max energy of 5, so a parent can actually afford it and remain
  viable with 2 energy left. The GUI uses a cost of 5; its HDC policy can rest to
  accumulate enough energy when conditions are favorable.
- `maxPopulation` (default 8, configurable): a cap on living agents. Reproduction
  fails once the population reaches it. The cap, not energy conservation, is what
  bounds growth: an offspring currently spawns at full health and energy, so total
  energy is *not* conserved across a birth — a documented limitation.

The offspring spawns on the first free tile found in the fixed order Up, Right,
Down, Left relative to the parent. If no adjacent tile is walkable and unoccupied,
reproduction fails and no energy is spent. The child inherits the parent's
*direction* and physical maxima (max health, damage, max energy, vision, inspect
ranges) and starts at full health and energy. Per-agent memory — transition counts,
visit maps, inspected crates — restarts fresh.

Reproduction is opt-in at two levels, which are deliberately distinct:

1. **Mechanics** (`Arena`): any brain may *attempt* `reproduce`, but the action only
   succeeds when the arena's `reproductionCost` and `maxPopulation` allow it.
2. **Autonomy** (`HdcBrain`): the HDC policy emits `reproduce` only when it is
   constructed with a positive reproduce-cost request and its local health/energy/
   hunger/space conditions are met (energy at the request, hunger below half, health
   at least half, a visible free adjacent tile, and an age of 80 ticks). Attempts
   are separated by at least 50 ticks. The bread-and-butter baselines
   (`RandomBrain`, `AggressiveBrain`) and `HdcBrain` with its default construction
   never reproduce.

A brain that does not override the new `AgentBrain::offspring()` hook returns
`nullptr`, and the reproduction attempt fails with `brain cannot reproduce`. Only
`HdcBrain` currently produces reproducing offspring.

`watch` is passive perception: it reveals location, direction, distance, and type, but not
health. `inspect` is an explicit action and returns the target's health in the next
observation. If the entity leaves the observed square before inspection is resolved, the
inspection fails rather than following a hidden entity ID. Inspecting a crate
reports its type and that it is movable; its presence is visible on a tile before
inspection. The inspection result refers to a position, not a persistent crate ID.

Visibility currently uses Manhattan distance and has no wall occlusion. Food is a tile
resource rather than a separate movable entity. These are deliberate baseline rules, not
implicit properties of a model.

## Adding a model-backed agent

Implement `AgentBrain` and attach it to an `ArenaEntity`:

```cpp
class ModelBrain final : public AgentBrain {
public:
    explicit ModelBrain(LocalRuntime runtime)
        : runtime_(std::move(runtime)) {}

    Action decide(const Observation& observation) override {
        // Encode only information available in this observation.
        const ModelInput input = encodeObservation(observation);

        // This may run a local CPU/GPU inference runtime. No network request is involved.
        const ModelOutput output = runtime_.infer(input, recurrentState_);
        recurrentState_ = output.nextState;

        return decodeAction(output.action);
    }

private:
    LocalRuntime runtime_;
    ModelState recurrentState_{};
};

Arena arena(20, 20);
auto brain = std::make_unique<ModelBrain>(loadLocalModel("model.bin"));
const EntityId id = arena.addAgent(ArenaEntity({4, 4}, std::move(brain)));
if (id < 0) {
    throw std::runtime_error("Could not place agent in the world");
}

const TickReport report = arena.step();
```

`LocalRuntime`, `ModelInput`, `ModelOutput`, `ModelState`, and the encoding/decoding
functions above are placeholders for the runtime and model adapter you choose. The arena
itself has no dependency on a particular ML framework. Each `AgentBrain` object persists
between ticks, so recurrent state and per-agent memory remain local to that agent.

Inference is currently synchronous. A slow model increases tick latency, but it does not
change the observation/action interface. Parallel inference can be added later by collecting
all inputs first, evaluating them in parallel, and preserving the common-snapshot and
resolution-order rules.

## Baselines and tests

Three brains are included:

- `RandomBrain` — seeded deterministic policy for smoke tests and reproducibility checks.
- `AggressiveBrain` — simple pursue-and-attack policy for checking entity interaction.
- `HdcBrain` — experimental online HDC transition model with a simple active-inference-inspired policy.

### HDC model prototype

`HdcBrain` encodes discretized health, energy, hunger, position, facing, nearby food,
forward terrain and crate, the last inspection and the nearest visible entity into a 256-dimensional bipolar
hypervector. Role/value vectors are generated deterministically from a seed and
bundled by majority vote. Similar observations are grouped into prototype
clusters. A new cluster is created whenever the nearest prototype has a Hamming
distance above 0.20. There is no cluster limit; memory and search cost can grow
without bound during long runs. Each action records counts of transitions to successor
clusters. After each action the next observation is compared with the previously
predicted prototype using normalized Hamming distance, and the transition is
learned online. Diagnostics expose the cluster count, learned transitions and
last prediction error (absent when there was no learned prediction).

The policy prioritizes exploration: it scores actions using count-based bonuses
for unfamiliar transitions and rarely observed successor clusters. Untried
actions are optimistically treated as potentially novel. Each brain also keeps a
private map of locations it has entered and favors moving into less-visited
cells, with a smaller bonus for turning toward them. Repeated reverse turns and
unnecessary waiting receive a small penalty; inspecting an unfamiliar crate gets
a bonus. Counts increment when entering a location, not while staying on it.
Expected health and
energy contribute only a small secondary preference (weight 0.05). A separate
hunger-dependent heuristic favors eating and moving or turning toward visible
food; this food drive is multiplied by a survival-urgency factor (1.5x each) when
the agent is hungry and when it is injured, so a starving or wounded agent seeks
food more aggressively. Clearly blocked
movement, eating without food and attacking without a target are excluded. The
agent also avoids eating while nearly full unless its health is critically low. Food
seeking is local and has no pathfinding, so walls may still trap the agent. This is an intentionally simplified
active-inference-inspired heuristic, **not** a full probabilistic generative model
or a computed variational/expected free energy. Predictions can be multimodal;
the displayed error compares against the most frequent successor, not the full
distribution. The model starts without training, has no persistence, and is not
guaranteed to survive or outperform the baselines. Its memory is private to each
agent. Resetting the GUI creates three fresh, independent models; the original
command-line demo remains unchanged.

### HDC inheritance and reproduction autonomy

`HdcBrain` can distil itself into a child via `offspring()`. The inherited subset is
chosen for a single, explicit pragmatic utility: **expected safety**, defined as the
learnt average of `(health/maxHealth + energy/maxEnergy) / 2` observed after each
action. This is maintained as a persistent per-cluster, per-action prior (`quality`)
that is updated online towards arriving-state safety, with a small initial prior weight.
It is independent
of the per-generation transition map precisely so it survives distillation.

Selection, not wholesale copying: `offspring()` keeps the top `MaxInheritedClusters`
(16) clusters ranked by evidence-weighted safety (`samples * (health + energy)`),
deterministically tie-broken by index. Each selected cluster contributes its
prototype, its `health`/`energy` safety summary, and its `quality` prior. Because the
prior is a fixed-size per-action value with *no* successor-cluster indices, selection
and renumbering need no transition remapping; transitions are deliberately not copied
(the child's per-agent memory restarts), which sidesteps index remapping altogether.

Inherited knowledge is used, not merely stored: while a cluster has no locally
observed transitions yet, the policy leans on the inherited `quality` prior
(a small bonus/penalty relative to 0.5). Once the child re-learns a cluster, the
normal empirical preference takes over. Crucially, the prior *itself* is carried
unchanged to grandchildren — a child does not recompute it from its own (still empty)
transitions, so lineage knowledge survives successive generations instead of
collapsing to neutral. Because the child keeps the parent's encoding seed, inherited
prototypes remain interpretable (its own `encode` produces the same hypervectors).

Diagnostics now surface this directly: `informedClusters` (clusters whose prior has
left neutral) and `maxPriorAdvantage` (largest `|quality - 0.5|`) are exposed through
`diagnostics()` and the GUI, alongside a read-only `actionSafety(observation)` view of
the safety estimate the policy currently holds per action.

Autonomous reproduction is disabled by default. Constructing an `HdcBrain` with a
positive reproduce-cost request (the second constructor argument) enables it: the
policy then chooses `reproduce` only when it has at least that much energy, its
    hunger is below half, its health is at least half, it can see a free adjacent tile,
    and it has lived for at least 80 ticks. When well-fed and healthy it may rest to
    save energy; it waits at least 50 ticks between reproduction attempts. The arena's own
`reproductionCost` and `maxPopulation` still gate whether the attempt succeeds. The
GUI backend now enables autonomous reproduction, and it discovers HDC diagnostics for
*all* living agents dynamically (via `ArenaEntity::getBrain()` + `dynamic_cast`),
rather than tracking a fixed three-element model pointer array, so descendants born
through reproduction appear in the display automatically.

The test suite covers movement, walls, food, combat, death, inspection, action conflicts,
rotating initiative, observation contents, invalid inputs, exception isolation, overflow
handling, deterministic seeded runs, long-run world occupancy invariants, asexual
reproduction (spawn and energy cost, energy requirement, population cap, no-free-tile
failure, cross-run determinism), HDC offspring distillation (safety-summary inheritance,
fresh per-agent memory, deterministic inheritance, non-reproducible brains), and the
survival-urgency food drive.

## Current limitations / next research steps

This is a usable simulation foundation, not yet a rich artificial-life environment. It has
cardinal movement, static walls, pushable crates, renewable food, facing-based melee, Manhattan visibility, and one
action per tick. It does not yet include line-of-sight occlusion, pathfinding, status effects,
crafting, object inventories, world serialization/replay files, parallel inference, or a
concrete neural-model runtime. Those can be added as separate systems without changing the
core `AgentBrain` observation/action contract unless an experiment explicitly needs a richer
interface.

Reproduction is the first experimental stage: the offspring currently spawns at full
health and energy, so total energy is *not* conserved across a birth (the parent pays the
cost, the child re-enters the world fully charged). Survival is bounded only by the
population cap and starvation; no guarantees of long-run stability should be inferred.
Autonomous HDC reproduction is enabled in the GUI, but not the demo. Room-level features (paths,
multi-room exploration advantages, cooperation) are deliberately deferred.
