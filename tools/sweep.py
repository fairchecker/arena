#!/usr/bin/env python3
"""Survival sweep: several brains over many different worlds (layout seeds).

Each (brain, layout seed) is one headless run of the GUI scenario without
reproduction. A layout seed changes source and start positions and the per-agent brain
seeds, so every seed is a genuinely different world (see --layout-seed in
run_experiment.py). The unit of replication is the world, not the agent: the three agents
of one world share food and interact, so results are summarised per world and the
confidence interval is a bootstrap over worlds.

Usage:
  python3 tools/sweep.py --binary build-release/bin/Release/arena_gui_backend \
      --brains hdc,graph,learned,greedy,random --layout-seeds 20 --ticks 2000 [--csv new.csv]
"""
import argparse
import json
import pathlib
import random
import statistics
import subprocess
import sys
from multiprocessing import Pool


def run_world(job):
    binary, brain, layout_seed, ticks, sources, random_food = job
    command = [binary, '--brain', brain, '--layout-seed', str(layout_seed), '--seed', str(layout_seed),
               '--sources-per-room', str(sources), '--no-reproduction']
    if not random_food:
        command.append('--no-random-food')
    process = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding='utf-8')
    try:
        def ask(name):
            process.stdin.write(name + '\n')
            process.stdin.flush()
            line = process.stdout.readline()
            if not line:
                raise RuntimeError(f'backend stopped (exit {process.poll()})')
            return json.loads(line)

        state = ask('state')
        ids = [a['id'] for a in state['agents']]
        lifetime = {i: ticks for i in ids}
        died = {i: False for i in ids}
        meals = {i: 0 for i in ids}
        for _ in range(ticks):
            state = ask('step')
            alive = {a['id'] for a in state['agents']}
            for action in state['actions']:
                if action['action'] == 'eat' and action['success']:
                    meals[action['id']] = meals.get(action['id'], 0) + 1
            for i in ids:
                if i not in alive and not died[i]:
                    died[i] = True
                    lifetime[i] = state['tick']
            if not alive:
                break
    finally:
        process.stdin.close()
        process.stdout.close()
        process.wait()
    return [{'brain': brain, 'layout_seed': layout_seed, 'agent': i, 'lifetime': lifetime[i],
             'died': died[i], 'meals': meals[i]} for i in ids]


def bootstrap_ci(values, repeats=2000, seed=0):
    rng = random.Random(seed)
    means = sorted(statistics.fmean(rng.choices(values, k=len(values))) for _ in range(repeats))
    return means[int(0.025 * repeats)], means[int(0.975 * repeats) - 1]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--binary', type=pathlib.Path, required=True)
    parser.add_argument('--brains', default='hdc,graph,learned,greedy,random')
    parser.add_argument('--layout-seeds', type=int, default=20, help='worlds per brain (seeds 1..N)')
    parser.add_argument('--ticks', type=int, default=2000)
    parser.add_argument('--sources-per-room', type=int, choices=range(1, 5), default=1)
    parser.add_argument('--no-random-food', action='store_true')
    parser.add_argument('--workers', type=int, default=2)
    parser.add_argument('--csv', type=pathlib.Path, help='write per-agent rows to this new file')
    args = parser.parse_args()
    if args.csv and args.csv.exists():
        parser.error(f'refusing to overwrite {args.csv}')
    binary = str(args.binary.resolve(strict=True))
    brains = args.brains.split(',')
    jobs = [(binary, b, s, args.ticks, args.sources_per_room, not args.no_random_food)
            for b in brains for s in range(1, args.layout_seeds + 1)]
    with Pool(args.workers) as pool:
        rows = [row for result in pool.imap_unordered(run_world, jobs) for row in result]
    if args.csv:
        with args.csv.open('x', encoding='utf-8') as out:
            out.write('brain,layout_seed,agent,lifetime,died,meals\n')
            for r in sorted(rows, key=lambda r: (r['brain'], r['layout_seed'], r['agent'])):
                out.write(f"{r['brain']},{r['layout_seed']},{r['agent']},{r['lifetime']},{int(r['died'])},{r['meals']}\n")
    print(f'{args.layout_seeds} worlds x 3 agents, horizon {args.ticks}, no reproduction, '
          f'{"no " if args.no_random_food else ""}random food, {args.sources_per_room} source(s)/room')
    print(f'{"brain":8s} {"agents alive at end":>20s} {"mean lifetime (95% CI over worlds)":>38s} '
          f'{"worlds, all 3 alive":>20s} {"meals/agent/1000t":>18s}')
    for brain in brains:
        mine = [r for r in rows if r['brain'] == brain]
        worlds = sorted({r['layout_seed'] for r in mine})
        per_world = []
        for w in worlds:
            agents = [r for r in mine if r['layout_seed'] == w]
            per_world.append(statistics.fmean(r['lifetime'] for r in agents))
        alive = sum(1 for r in mine if not r['died'])
        all_alive = sum(1 for w in worlds if all(not r['died'] for r in mine if r['layout_seed'] == w))
        low, high = bootstrap_ci(per_world)
        alive_ticks = sum(r['lifetime'] for r in mine)
        meals = 1000.0 * sum(r['meals'] for r in mine) / max(1, alive_ticks)
        print(f'{brain:8s} {alive:>11d}/{len(mine):<8d} {statistics.fmean(per_world):>14.0f}  [{low:.0f}, {high:.0f}]'
              f'{all_alive:>16d}/{len(worlds):<3d} {meals:>18.2f}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f'sweep failed: {error}', file=sys.stderr)
        sys.exit(1)
