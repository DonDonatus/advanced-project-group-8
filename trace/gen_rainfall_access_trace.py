#!/usr/bin/env python3
"""
gen_rainfall_access_trace.py
CPEN 438 Project 8 -- "Ahead of the Storm"

Generates two memory-access traces simulating a program that reads
rainfall-gauge readings from a 2-D grid covering the Volta basin.

  - STRIDE-REGULAR trace : a full row-major sweep across the grid
                            (constant stride between consecutive accesses)
  - IRREGULAR trace      : repeated "look at a random gauge, then its
                            four neighbours" pattern (flood-propagation
                            style lookup -- no constant stride)

Both traces are plain text files: one memory address (in bytes) per line.
Addresses are reproducible for a given --seed, so re-running with the
same seed always gives the same trace (important for grading and for
comparing your hand-trace in Week 1 against the real file).

Usage:
    python3 gen_rainfall_access_trace.py --seed 8 \
        --width 64 --height 64 --num-irregular 2000 \
        --out-stride stride_regular.trace --out-irregular irregular.trace

Each team should run this ONCE with their assigned seed (your group
number is a fine seed to use) and then treat the two output files as
fixed input data for the rest of the semester -- do not regenerate
with a different seed later, or your Week 1 hand-trace will no longer
match your Week 3 experiment results.
"""

import argparse
import random


CELL_SIZE_BYTES = 8          # each rainfall reading is a double (8 bytes)
BASE_ADDRESS = 0x10000000    # arbitrary base address for the grid


def cell_address(row, col, width):
    """Byte address of grid cell (row, col) in a row-major WxH grid."""
    index = row * width + col
    return BASE_ADDRESS + index * CELL_SIZE_BYTES


def generate_stride_regular(width, height, passes, out_path):
    """
    Row-major sweep: for each row, visit every column left to right.
    Consecutive addresses differ by exactly CELL_SIZE_BYTES -- a
    constant stride -- which is exactly the pattern a stride-detecting
    prefetcher is designed to catch.
    """
    with open(out_path, "w") as f:
        for _ in range(passes):
            for row in range(height):
                for col in range(width):
                    f.write(f"{cell_address(row, col, width)}\n")


def generate_irregular(width, height, num_accesses, seed, out_path):
    """
    Simulates a flood-propagation-style kernel: pick a random gauge,
    then look at its four orthogonal neighbours (N/S/E/W), then pick
    another random gauge, and so on. This has NO constant stride --
    it is the pattern that defeats next-line and stride prefetchers,
    and is what a stream buffer / correlation prefetcher is meant to
    help with instead.
    """
    rng = random.Random(seed)
    with open(out_path, "w") as f:
        accesses_written = 0
        while accesses_written < num_accesses:
            row = rng.randrange(1, height - 1)   # avoid edges so all
            col = rng.randrange(1, width - 1)    # 4 neighbours exist

            # the "anchor" gauge itself
            f.write(f"{cell_address(row, col, width)}\n")
            accesses_written += 1

            # its four neighbours, in a fixed N/S/E/W order
            neighbours = [
                (row - 1, col),
                (row + 1, col),
                (row, col - 1),
                (row, col + 1),
            ]
            for nr, nc in neighbours:
                if accesses_written >= num_accesses:
                    break
                f.write(f"{cell_address(nr, nc, width)}\n")
                accesses_written += 1


def main():
    ap = argparse.ArgumentParser(description="Generate rainfall access traces")
    ap.add_argument("--seed", type=int, required=True,
                     help="Your team's assigned seed (e.g. your group number)")
    ap.add_argument("--width", type=int, default=64, help="Grid width in cells")
    ap.add_argument("--height", type=int, default=64, help="Grid height in cells")
    ap.add_argument("--passes", type=int, default=1,
                     help="How many full sweeps for the stride-regular trace")
    ap.add_argument("--num-irregular", type=int, default=2000,
                     help="Number of addresses in the irregular trace")
    ap.add_argument("--out-stride", default="stride_regular.trace")
    ap.add_argument("--out-irregular", default="irregular.trace")
    args = ap.parse_args()

    generate_stride_regular(args.width, args.height, args.passes, args.out_stride)
    generate_irregular(args.width, args.height, args.num_irregular,
                        args.seed, args.out_irregular)

    print(f"Grid size            : {args.width} x {args.height} cells")
    print(f"Seed used            : {args.seed}")
    print(f"Stride-regular trace : {args.out_stride} "
          f"({args.width * args.height * args.passes} addresses)")
    print(f"Irregular trace      : {args.out_irregular} "
          f"({args.num_irregular} addresses)")
    print("\nKeep this seed recorded in your Week 1 Project Proposal.")


if __name__ == "__main__":
    main()
