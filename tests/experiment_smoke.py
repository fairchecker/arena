import json
import pathlib
import subprocess
import sys
import tempfile

runner, binary, trainer = sys.argv[1:]
with tempfile.TemporaryDirectory() as temporary:
    root = pathlib.Path(temporary)
    def run(directory):
        return subprocess.run([sys.executable, runner, '--binary', binary, '--ticks', '100',
                               '--seed', '42', '--snapshot-every', '40', '--output', str(directory)],
                              capture_output=True, text=True)
    first, second = root / 'one', root / 'two'
    assert run(first).returncode == 0
    assert run(second).returncode == 0
    assert run(first).returncode != 0  # Never overwrite an existing experiment.
    for name in ('metadata.json', 'ticks.jsonl', 'snapshots.jsonl'):
        assert (first / name).read_bytes() == (second / name).read_bytes()
    ticks = [json.loads(line) for line in (first / 'ticks.jsonl').read_text().splitlines()]
    snapshots = [json.loads(line) for line in (first / 'snapshots.jsonl').read_text().splitlines()]
    assert len(ticks) == 100 and ticks[-1]['tick'] == 100
    assert [s['tick'] for s in snapshots] == [0, 40, 80, 100]
    cluster = snapshots[-1]['models'][0]['brain']['clusters'][0]
    assert len(cluster['prototype']) == len(cluster['sums']) == 256
    assert len(cluster['quality']) == len(cluster['transitions']) == 9
    assert 'message' in ticks[-1]['actions'][0]
    for sources in (1, 4):
        process = subprocess.run([binary, '--seed', '42', '--sources-per-room', str(sources),
                                  '--no-random-food', '--no-reproduction'],
                                 input='state\n' + 'step\n' * 60 + 'reset\n',
                                 capture_output=True, text=True, check=True)
        states = [json.loads(line) for line in process.stdout.splitlines()]
        assert states[0] == states[-1]
        assert sum(tile['source'] for tile in states[0]['tiles']) == sources * 9
        assert sum(tile['food'] for tile in states[0]['tiles']) == sources * 9
        assert all(tile['source'] or tile['food'] == 0 for state in states for tile in state['tiles'])
        assert all(a['action'] != 'reproduce' for state in states for a in state['actions'])
    graph_output = root / 'graph'
    subprocess.run([sys.executable, runner, '--binary', binary, '--brain', 'graph',
                    '--ticks', '100', '--seed', '42', '--no-random-food',
                    '--snapshot-every', '50', '--output', str(graph_output)], check=True)
    graph_ticks = [json.loads(line) for line in (graph_output / 'ticks.jsonl').read_text().splitlines()]
    assert all(m['type'] == 'graph' for state in graph_ticks for m in state['models'])
    graph_snapshots = [json.loads(line) for line in (graph_output / 'snapshots.jsonl').read_text().splitlines()]
    assert graph_snapshots[-1]['models'][0]['brain']['places']
    assert not json.loads((graph_output / 'metadata.json').read_text())['reproduction']
    learned_output = root / 'learned'
    subprocess.run([sys.executable, runner, '--binary', binary, '--brain', 'learned',
                    '--ticks', '30', '--seed', '42', '--no-random-food',
                    '--snapshot-every', '15', '--output', str(learned_output)], check=True)
    learned_snapshots = [json.loads(line) for line in (learned_output / 'snapshots.jsonl').read_text().splitlines()]
    model = learned_snapshots[-1]['models'][0]['brain']
    assert model['type'] == 'learned' and model['states'] and model['replay']
    assert len(model['states'][0]['values']) == 5
    skill_dir = root / 'skills'
    subprocess.run([trainer, '--train-output', str(skill_dir)], check=True)
    assert subprocess.run([trainer, '--train-output', str(skill_dir)], capture_output=True).returncode != 0
    skill_file = skill_dir / 'skills.txt'
    output = root / 'pretrained'
    subprocess.run([sys.executable, runner, '--binary', binary, '--brain', 'learned',
                    '--skills', str(skill_file), '--ticks', '10', '--seed', '42',
                    '--snapshot-every', '10', '--output', str(output)], check=True)
    initial = json.loads((output / 'snapshots.jsonl').read_text().splitlines()[0])
    assert len(initial['models']) == 3
    assert all(m['brain']['states'] for m in initial['models'])
    assert len(json.loads((output / 'metadata.json').read_text())['skillsSha256']) == 64

    # --layout-seed: same seed -> same world, different seed -> different world, and the
    # default (no flag) layout stays the historical fixed one. Baseline brains run.
    def world(*extra):
        process = subprocess.run([binary, '--no-random-food', '--no-reproduction', *extra],
                                 input='state\n', capture_output=True, text=True, check=True)
        state = json.loads(process.stdout.splitlines()[0])
        sources = [i for i, tile in enumerate(state['tiles']) if tile['source']]
        starts = [(a['row'], a['column']) for a in state['agents']]
        return sources, starts
    assert world('--layout-seed', '1') == world('--layout-seed', '1')
    assert world('--layout-seed', '1') != world('--layout-seed', '2')
    assert world('--brain', 'hdc') == world('--brain', 'greedy')  # default layout is brain-independent
    assert world('--layout-seed', '1')[0] != world()[0]
    assert len(world('--layout-seed', '3')[0]) == 9
    assert len(set(world('--layout-seed', '3')[1])) == 3
    for brain in ('random', 'greedy'):
        process = subprocess.run([binary, '--brain', brain, '--layout-seed', '5', '--no-random-food',
                                  '--no-reproduction'], input='state\n' + 'step\n' * 40,
                                 capture_output=True, text=True, check=True)
        states = [json.loads(line) for line in process.stdout.splitlines()]
        assert states[-1]['tick'] == 40
        assert all(m['type'] == brain for m in states[-1]['models'])
