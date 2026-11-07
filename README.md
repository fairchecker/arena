# Arena

A deterministic, in-process environment for experiments with embodied agents. Agents are
ordinary entities in the world. Each entity owns a decision component (`AgentBrain`) that
receives an observation and returns an action. Inference runs locally in the simulation
process: there is no HTTP server, gateway, socket protocol, or remote-agent abstraction.

## Requirements

- CMake 3.16 or newer
- A C++17 compiler (GCC, Clang, or MSVC)

## Build and run

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel
ctest --test-dir build --output-on-failure
./build/arena_demo
```

To run with AddressSanitizer and UndefinedBehaviorSanitizer (GCC/Clang):

```bash
cmake -S . -B build-sanitize \
  -DCMAKE_BUILD_TYPE=Debug \
  -DARENA_ENABLE_SANITIZERS=ON
cmake --build build-sanitize --parallel
ctest --test-dir build-sanitize --output-on-failure
```

CMake presets (CMake 3.21+) are also provided: `release` and `sanitize`.

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
5. The tick counter advances and the report records the resulting number of living agents.

This gives models a consistent observation snapshot while retaining deterministic conflict
resolution. The resolution order is part of the environment semantics and should be kept
fixed when comparing model versions.

### Observation and action space

`Observation` contains:

- tick number;
- the agent's position, direction, current and maximum health/energy, damage, and perception
  ranges;
- visible tiles (walkability and food amount);
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
| `eat` | Consume 1 food on the current tile; restore up to 2 health and 2 energy. |
| `inspect(position)` | Actively inspect a currently visible entity within inspect range. |

Failed moves and attacks do not consume energy. Rotation and inspection are currently free.

`watch` is passive perception: it reveals location, direction, distance, and type, but not
health. `inspect` is an explicit action and returns the target's health in the next
observation. If the entity leaves the observed square before inspection is resolved, the
inspection fails rather than following a hidden entity ID.

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

Two baseline brains are included:

- `RandomBrain` — seeded deterministic policy for smoke tests and reproducibility checks.
- `AggressiveBrain` — simple pursue-and-attack policy for checking entity interaction.

The test suite covers movement, walls, food, combat, death, inspection, action conflicts,
rotating initiative, observation contents, invalid inputs, exception isolation, overflow
handling, deterministic seeded runs, and long-run world occupancy invariants.

## Current limitations / next research steps

This is a usable simulation foundation, not yet a rich artificial-life environment. It has
cardinal movement, static walls, food, facing-based melee, Manhattan visibility, and one
action per tick. It does not yet include line-of-sight occlusion, pathfinding, status effects,
crafting, object inventories, world serialization/replay files, parallel inference, or a
concrete neural-model runtime. Those can be added as separate systems without changing the
core `AgentBrain` observation/action contract unless an experiment explicitly needs a richer
interface.
