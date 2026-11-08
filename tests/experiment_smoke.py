import json
import pathlib
import subprocess
import sys
import tempfile

runner, binary = sys.argv[1:]
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
