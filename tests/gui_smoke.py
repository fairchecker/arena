import json
import subprocess
import sys
import urllib.error
import urllib.request

backend, server = sys.argv[1:]
process = subprocess.Popen([sys.executable, server, '--binary', backend, '--port', '0', '--no-browser', '--seed', '42'],
                           stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
try:
    line = process.stdout.readline()
    if not line.startswith('Arena GUI: '):
        raise RuntimeError('Server failed to start: ' + process.stderr.read())
    url = line.split()[2]
    origin = url.rstrip('/')
    with urllib.request.urlopen(url, timeout=5) as response:
        assert b'<canvas' in response.read()

    def command(name):
        request = urllib.request.Request(url + name, data=b'', headers={
            'Origin': origin, 'X-Arena-Request': '1'})
        with urllib.request.urlopen(request, timeout=5) as response:
            return json.load(response)

    initial = command('state')
    assert initial['tick'] == 0 and initial['alive'] == 3
    assert (initial['width'], initial['height']) == (36, 24)
    assert len(initial['models']) == 3 and all(model['clusters'] == 0 for model in initial['models'])
    assert initial['agents'][0]['hunger'] == 0
    assert len(initial['tiles']) == initial['width'] * initial['height']
    assert sum(tile['food'] for tile in initial['tiles']) == 17
    assert sum(tile['crate'] for tile in initial['tiles']) == 6
    assert sum(tile['source'] for tile in initial['tiles']) == 9
    # The rooms must be connected without moving a crate.
    width, height = initial['width'], initial['height']
    start = (initial['agents'][0]['row'], initial['agents'][0]['column'])
    reachable, pending = {start}, [start]
    while pending:
        row, col = pending.pop()
        for r, c in ((row - 1, col), (row + 1, col), (row, col - 1), (row, col + 1)):
            if 0 <= r < height and 0 <= c < width:
                tile = initial['tiles'][r * width + c]
                if not tile['wall'] and not tile['crate'] and (r, c) not in reachable:
                    reachable.add((r, c))
                    pending.append((r, c))
    assert all((a['row'], a['column']) in reachable for a in initial['agents'])
    assert all(tile['wall'] or tile['crate'] or (i // width, i % width) in reachable
               for i, tile in enumerate(initial['tiles']))
    stepped = command('step')
    assert stepped['tick'] == 1 and all(model['clusters'] == 1 for model in stepped['models'])
    assert all(model['visited'] == 1 and len(model['visits']) == 1 for model in stepped['models'])
    assert len({tuple(model['visits'][0][:2]) for model in stepped['models']}) == 3
    assert stepped['agents'][0]['hunger'] == 1
    added_food = 0
    food_count = sum(tile['food'] for tile in stepped['tiles'])
    history = [stepped]
    for _ in range(99):
        stepped = command('step')
        next_count = sum(tile['food'] for tile in stepped['tiles'])
        consumed = sum(1 for a in stepped['actions'] if a['action'] == 'eat' and a['success'])
        added_food += next_count - food_count + consumed
        food_count = next_count
        history.append(stepped)
    assert added_food > 0, 'Food did not spawn'
    assert all(model['visited'] > 1 for model in stepped['models'])
    assert all(model['visited'] == len(model['visits']) for model in stepped['models'])
    assert command('reset') == initial
    for expected in history:
        assert command('step') == expected, 'Reset did not reproduce the seeded simulation'
    births = 0
    for _ in range(300):
        stepped = command('step')
        births += sum(a['action'] == 'reproduce' and a['success'] for a in stepped['actions'])
        assert {m['id'] for m in stepped['models']} == {a['id'] for a in stepped['agents']}
        assert stepped['alive'] <= 8
    assert births > 0, 'No autonomous offspring appeared in the seeded GUI scenario'
    try:
        urllib.request.urlopen(urllib.request.Request(url + 'step', data=b''), timeout=5)
        raise AssertionError('Cross-origin request was accepted')
    except urllib.error.HTTPError as exc:
        assert exc.code == 403
    print('GUI HTTP and simulation smoke test passed')
finally:
    process.terminate()
    process.wait(timeout=5)
