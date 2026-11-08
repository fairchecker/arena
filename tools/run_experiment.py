#!/usr/bin/env python3
"""Run the GUI scenario headlessly; export JSONL telemetry and HDC snapshots."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys
import time


def positive(value):
    result = int(value)
    if result <= 0:
        raise argparse.ArgumentTypeError('must be positive')
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ticks', type=positive, required=True)
    parser.add_argument('--seed', type=int, required=True)
    parser.add_argument('--no-reproduction', action='store_true')
    parser.add_argument('--brain', choices=('hdc', 'graph', 'learned'), default='hdc')
    parser.add_argument('--skills', type=pathlib.Path, help='LearnedBrain skill file')
    parser.add_argument('--no-random-food', action='store_true')
    parser.add_argument('--sources-per-room', type=int, choices=range(1, 5), default=1)
    parser.add_argument('--snapshot-every', type=positive, default=1000)
    parser.add_argument('--output', type=pathlib.Path, required=True,
                        help='new output directory (must not already exist)')
    parser.add_argument('--binary', type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / 'build-release/bin/Release/arena_gui_backend')
    args = parser.parse_args()
    if not 0 <= args.seed <= 0xffffffff:
        parser.error('seed must be between 0 and 4294967295')
    if args.skills and args.brain != 'learned':
        parser.error('--skills requires --brain learned')
    skills = args.skills.resolve(strict=True) if args.skills else None
    binary = args.binary.resolve(strict=True)
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {'formatVersion': 1, 'seed': args.seed, 'requestedTicks': args.ticks,
                'snapshotEvery': args.snapshot_every, 'scenario': 'gui', 'brain': args.brain,
                'reproduction': args.brain == 'hdc' and not args.no_reproduction,
                'sourcesPerRoom': args.sources_per_room, 'randomFood': not args.no_random_food,
                'skillsSha256': hashlib.sha256(skills.read_bytes()).hexdigest() if skills else None,
                'binary': str(binary), 'binarySha256': hashlib.sha256(binary.read_bytes()).hexdigest()}
    (args.output / 'metadata.json').write_text(json.dumps(metadata, indent=2) + '\n', encoding='utf-8')
    start = time.monotonic()
    command_line = [str(binary), '--seed', str(args.seed)]
    if args.no_reproduction:
        command_line.append('--no-reproduction')
    if args.no_random_food:
        command_line.append('--no-random-food')
    command_line += ['--sources-per-room', str(args.sources_per_room)]
    command_line += ['--brain', args.brain]
    if skills:
        command_line += ['--skills', str(skills)]
    process = subprocess.Popen(command_line, stdin=subprocess.PIPE,
                               stdout=subprocess.PIPE, text=True, encoding='utf-8')

    def command(name):
        process.stdin.write(name + '\n')
        process.stdin.flush()
        line = process.stdout.readline()
        if not line:
            raise RuntimeError(f'backend stopped unexpectedly (exit {process.poll()})')
        result = json.loads(line)
        if 'error' in result:
            raise RuntimeError(result['error'])
        return result

    def write(file, value):
        file.write(json.dumps(value, separators=(',', ':'), allow_nan=False) + '\n')

    completed = 0
    try:
        with (args.output / 'ticks.jsonl').open('w', encoding='utf-8') as ticks, \
             (args.output / 'snapshots.jsonl').open('w', encoding='utf-8') as snapshots:
            state = command('state')
            write(snapshots, command('snapshot'))
            for _ in range(args.ticks):
                if not state['alive']:
                    break
                state = command('step')
                completed = state['tick']
                tiles = state.pop('tiles')
                state['foodTotal'] = sum(tile['food'] for tile in tiles)
                # Detailed visit maps are in snapshots; telemetry retains all scalar diagnostics.
                for model in state['models']:
                    model.pop('visits')
                write(ticks, state)
                if completed % args.snapshot_every == 0:
                    write(snapshots, command('snapshot'))
            if completed % args.snapshot_every != 0:
                write(snapshots, command('snapshot'))
    finally:
        process.stdin.close()
        process.stdout.close()
        process.wait()
    if process.returncode:
        raise RuntimeError(f'backend exited with status {process.returncode}')
    print(f'Completed {completed} ticks in {time.monotonic() - start:.2f}s; output: {args.output}')


if __name__ == '__main__':
    try:
        main()
    except (OSError, ValueError, RuntimeError) as error:
        print(f'experiment failed: {error}', file=sys.stderr)
        sys.exit(1)
