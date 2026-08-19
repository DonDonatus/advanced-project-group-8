#!/usr/bin/env python3
"""
run_experiment_matrix.py
CPEN 438 Project 8 -- "Ahead of the Storm"

Runs the compiled prefetch_sim across every combination of:
    prefetcher mode  x  trace file
and prints a single summary table (also saved as results.csv).

This is a Week 3 tool -- you won't get meaningful stride/streambuf
numbers out of it until those TODOs in starter_prefetch_sim.c are
implemented and passing your Week 1 hand-trace check. It's included
now so you know exactly what Week 3 will ask you to produce.

Usage (run from the analysis/ folder, after building ../src/prefetch_sim):
    python3 run_experiment_matrix.py
"""

import subprocess
import csv
import os

SIM_BINARY = "../src/prefetch_sim"
MODES = ["none", "nextline", "stride", "streambuf"]
TRACES = {
    "stride_regular": "../trace/stride_regular.trace",
    "irregular":      "../trace/irregular.trace",
}


def run_one(trace_path, mode):
    result = subprocess.run(
        [SIM_BINARY, trace_path, mode],
        capture_output=True, text=True, check=True
    )
    stats = {}
    for line in result.stdout.splitlines():
        if ":" in line:
            key, val = line.split(":", 1)
            stats[key.strip()] = val.strip()
    return stats


def main():
    if not os.path.exists(SIM_BINARY):
        print(f"Could not find {SIM_BINARY} -- build it first with:")
        print("  cd ../src && gcc -O2 -o prefetch_sim starter_prefetch_sim.c")
        return

    rows = []
    for trace_name, trace_path in TRACES.items():
        for mode in MODES:
            stats = run_one(trace_path, mode)
            rows.append({
                "trace": trace_name,
                "mode": mode,
                "hit_rate": stats.get("Hit rate", ""),
                "coverage": stats.get("Coverage", ""),
                "accuracy": stats.get("Accuracy", ""),
                "mlp": stats.get("MLP (avg outstanding)", ""),
            })

    # Print a readable table
    header = f"{'trace':16} {'mode':10} {'hit_rate':9} {'coverage':9} {'accuracy':9} {'mlp':6}"
    print(header)
    print("-" * len(header))
    for r in rows:
        print(f"{r['trace']:16} {r['mode']:10} {r['hit_rate']:9} "
              f"{r['coverage']:9} {r['accuracy']:9} {r['mlp']:6}")

    with open("results.csv", "w", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)
    print("\nSaved to results.csv")


if __name__ == "__main__":
    main()
