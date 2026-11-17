#!/usr/bin/env python3
"""Run baseline vs challenge-1 optimized clox on BOTH types of workloads.

Build before running:
  gcc -O2 -std=c99 src/*.c -o clox-baseline  # BEFORE applying Challenge 1
  gcc -O2 -std=c99 src/*.c -o clox-ch1        # AFTER applying Challenge 1
  python3 tests/ch25_challenge1/benchmark.py
"""
import csv
import statistics
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
TESTS = [
    ('fib_no_closure.lox', '2584'),
    ('ordinary_function_creation.lox', '250000'),
    ('closure_creation.lox', '299997'),
    ('reused_closure.lox', '200000'),
]
RUNS = 5


def execute(binary, path, expected):
    proc = subprocess.run([str(binary), str(path)], cwd=ROOT,
                          capture_output=True, text=True, timeout=120)
    if proc.returncode != 0:
        raise RuntimeError(f'{binary.name} / {path.name}: {proc.stderr}')
    lines = proc.stdout.strip().splitlines()
    if len(lines) != 2 or lines[0] != expected:
        raise RuntimeError(f'{binary.name} / {path.name}: expected {expected}, got {lines!r}')
    elapsed = float(lines[1])
    if elapsed < 0:
        raise RuntimeError(f'Invalid elapsed time: {elapsed}')
    return elapsed


def main():
    baseline = ROOT / 'clox-baseline'
    optimized = ROOT / 'clox-ch1'
    for binary in (baseline, optimized):
        if not binary.exists():
            raise SystemExit(f'Missing {binary}. See the setup in benchmark.py.')

    print('Measured using Lox clock() CPU time; both binaries built with -O2.')
    print(f'{RUNS} repetitions per program per binary; lower is better.\n')
    results = []
    for script, expected in TESTS:
        measured = {'baseline': [], 'optimized': []}
        # Alternate execution order across repetitions to reduce run-order bias.
        for rep in range(RUNS):
            order = [('baseline', baseline), ('optimized', optimized)]
            if rep % 2: order.reverse()
            for label, binary in order:
                measured[label].append(execute(binary, HERE / script, expected))
        b = statistics.median(measured['baseline'])
        o = statistics.median(measured['optimized'])
        change = (b / o - 1) * 100 if o else float('inf')
        results.append((script, b, o, change))
        print(f'{script}: baseline={b:.6f}s, optimized={o:.6f}s, speedup={change:+.1f}%')

    output = ROOT / 'ch25_challenge1_benchmark_results.csv'
    with output.open('w', newline='') as f:
        writer = csv.writer(f)
        writer.writerow(['program', 'baseline_median_cpu_seconds',
                         'optimized_median_cpu_seconds', 'speedup_percent',
                         'runs', 'compiler_flags'])
        for script, b, o, speedup in results:
            writer.writerow([script, f'{b:.7f}', f'{o:.7f}',
                             f'{speedup:.2f}', RUNS, '-O2 -std=c99'])
    print(f'\nPASS: benchmark outputs matched expected values. Saved {output.name}')


if __name__ == '__main__':
    main()
