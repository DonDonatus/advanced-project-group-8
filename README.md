# Ahead of the Storm — CPEN 438 Project 8

A trace-driven cache prefetcher simulator. Five prefetch policies are implemented
and compared against two synthetic traces: `none`, `nextline`, `stride`,
`streambuf` (Jouppi stream buffer), and `hybrid` (a stride/delta-correlation
arbiter, Week 4).

## Build

From `src/`:

```
gcc -O2 -Wall -o prefetch_sim starter_prefetch_sim.c
```

`starter_prefetch_sim.c` is the single translation unit for the whole
simulator — it `#include`s `hybrid_prefetch_stub.c` directly, so nothing else
needs to be compiled or linked separately.

## Run

```
./prefetch_sim <trace_file> <none|nextline|stride|streambuf|hybrid>
```

Example:

```
./prefetch_sim ../trace/irregular.trace hybrid
```

## Test

Two unit test binaries `#include` `starter_prefetch_sim.c` directly so they
can never drift from the shipped simulator logic:

```
gcc -O2 -Wall -DUNIT_TEST_BUILD -o test_prefetch test_prefetch.c
./test_prefetch
```
Regression gate for `none`/`nextline`/`stride`/`streambuf` (Week 2).

```
gcc -O2 -Wall -DUNIT_TEST_BUILD -o test_hybrid test_hybrid.c
./test_hybrid
```
H1–H6 gate for `hybrid` (Week 4). Both must show `0 failure(s)` before hybrid
results are trusted.

## Run the full experiment matrix

`run_experiment_matrix.py` lives at the repo root but resolves its paths
relative to its **working directory**, not its own location — run it with
`analysis/` as the current directory:

```
cd analysis
python ../run_experiment_matrix.py
```

It expects the compiled binary at `../src/prefetch_sim` (no `.exe` — on
Windows, build normally and also keep an extension-less copy alongside
`prefetch_sim.exe`, since the script's own `os.path.exists()` check looks for
the exact name `prefetch_sim`). It regenerates `analysis/results.csv`.

## Current results (`analysis/results.csv`)

| trace | mode | hit_rate | coverage | accuracy | mlp |
|---|---|---|---|---|---|
| stride_regular | none | 0.8750 | 0.0000 | 0.0000 | 0.0000 |
| stride_regular | nextline | 0.9375 | 0.5000 | 1.0000 | 0.5000 |
| stride_regular | stride | 0.9062 | 0.2500 | 1.0000 | 0.2500 |
| stride_regular | streambuf | 0.9998 | 0.9980 | 0.4981 | 4.0000 |
| stride_regular | **hybrid** | 0.9373 | **0.4980** | 1.0000 | 0.4980 |
| irregular | none | 0.4440 | 0.0000 | 0.0000 | 0.0000 |
| irregular | nextline | 0.4635 | 0.0853 | 0.1047 | 26.2790 |
| irregular | stride | 0.4440 | 0.0000 | 0.0000 | 0.0000 |
| irregular | streambuf | 0.4450 | 0.0398 | 0.0102 | 4.0000 |
| irregular | **hybrid** | 0.4440 | **0.0000** | 0.0000 | 0.0000 |

`distance_sweep.csv` sweeps `STREAMBUF_DEPTH` and is specific to the stream
buffer; hybrid has no equivalent depth parameter, so it does not appear there.

## Hybrid prefetcher (Week 4)

`hybrid_prefetch()` arbitrates between a stride predictor (assume the last
delta repeats) and a delta-correlation predictor (a small table keyed on
"what delta followed the last observed delta"), scoring both against what
actually happens and issuing at most one prefetch per miss from whichever is
currently ahead — ties favour stride. Confidence in a correlation-table entry
must clear `CONF_THRESHOLD` (2 confirmations) before it is trusted enough to
act on; see the `WHY THIS RUNS ON EVERY ACCESS` comment in
`hybrid_prefetch_stub.c` for the reasoning behind training on hits as well as
misses, and the `delta == 0` early-return for why same-line re-references are
skipped entirely.

**Findings worth noting for the report / live defence:**
- On `stride_regular.trace`, hybrid strictly beats plain `stride` (0.498 vs.
  0.25 coverage) at the same 1.0 accuracy — correlation reinforces stride's
  own +1 prediction once trained.
- On `irregular.trace`, hybrid ties `stride` at zero issued prefetches. This
  is not a bug: traced against the real trace, the correlation table
  correctly learns the -8/+16 transition and reaches confidence, but every
  time it is confident enough to act, the predicted line is already resident
  in the 64-line cache — the grid neighbourhood this trace walks is small
  enough that nothing ever actually needs re-fetching. The mechanism works;
  it has nothing to gain on this specific trace/cache-size combination.

## Repo layout

```
src/starter_prefetch_sim.c   simulator core + all five prefetch policies
src/hybrid_prefetch_stub.c   hybrid predictor/arbiter (Week 4)
src/demo_prefetcher.c        standalone next-line teaching example (not part of the build)
src/test_prefetch.c          Week 2 regression gate
src/test_hybrid.c            Week 4 (H1-H6) gate
trace/                       input traces + generator
analysis/                    results.csv, distance_sweep.csv, MLP model
docs/                        supporting figures
```
