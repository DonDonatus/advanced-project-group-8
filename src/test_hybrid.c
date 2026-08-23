/*
 * test_hybrid.c
 * CPEN 438 Project 8 -- Week 3 Level-3 innovation gate.
 *
 * Same pattern as test_prefetch.c: #includes the real simulator source
 * so the tests cannot drift from the shipped implementation.
 *
 * THESE TESTS ARE EXPECTED TO FAIL until hybrid_prefetch() and its two
 * helpers are implemented. That is the point -- they are the Week 3
 * equivalent of the Week 2 hand-trace gate, written before the code.
 *
 * Build (after wiring PREFETCH_HYBRID into starter_prefetch_sim.c):
 *   gcc -O2 -Wall -DUNIT_TEST_BUILD -o test_hybrid test_hybrid.c
 * Run:
 *   ./test_hybrid
 */

#include <stdio.h>

#define main sim_main_unused
#include "starter_prefetch_sim.c"
#undef main

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

static void reset_all(void) {
    cache_init();
    global_tick = 0;
    outstanding_prefetches = 0;
    stat_accesses = stat_hits = stat_misses = 0;
    stat_prefetches_issued = stat_prefetches_useful = stat_prefetches_wasted = 0;
    mlp_sum = 0.0; mlp_samples = 0;
    last_miss_addr = -1; last_stride = 0;
    stream_buf_next_fetch = -1;
    for (int i = 0; i < STREAMBUF_DEPTH; i++) stream_buf_valid[i] = 0;
    hybrid_init();
    g_mode = PREFETCH_HYBRID;
}

/* H1: on a clean constant stride the hybrid must behave like the stride
 *     prefetcher -- it must NOT lose ground to it on stride_regular. */
static void test_hybrid_matches_stride_on_constant_stride(void) {
    reset_all();
    access_address(0 * LINE_SIZE);
    access_address(4 * LINE_SIZE);           /* delta = +4, unconfirmed */
    long issued_before = stat_prefetches_issued;
    access_address(8 * LINE_SIZE);           /* delta = +4 again -> confirmed */
    CHECK(stat_prefetches_issued == issued_before + 1,
          "H1 hybrid issues exactly one prefetch once a stride repeats twice");
}

/* H2: the arbiter must prefer stride on a tie. Fresh state, both scores
 *     zero, constant stride -> the stride path must be the one credited. */
static void test_hybrid_prefers_stride_on_tie(void) {
    reset_all();
    access_address(0 * LINE_SIZE);
    access_address(4 * LINE_SIZE);
    access_address(8 * LINE_SIZE);
    CHECK(stat_hybrid_stride_issued > 0 && stat_hybrid_corr_issued == 0,
          "H2 arbiter breaks ties toward the stride predictor");
}

/* H3: THE HEADLINE TEST. A cycling delta pattern that the Week 2 stride
 *     prefetcher provably cannot touch (it issued 0 prefetches on the
 *     whole irregular trace) must produce prefetches here.
 *     Pattern: repeating -8, +16 hops, i.e. the anchor -> N -> S motif
 *     that dominates irregular.trace. */
static void test_hybrid_learns_cycling_pattern(void) {
    reset_all();
    long line = 1000;
    /* Walk the cycle enough times for confidence to saturate. */
    for (int i = 0; i < 12; i++) {
        access_address(line * LINE_SIZE);          line -= 8;
        access_address(line * LINE_SIZE);          line += 16;
    }
    CHECK(stat_hybrid_corr_issued > 0,
          "H3 hybrid issues correlation prefetches on a cycling -8/+16 pattern");
    CHECK(stat_hybrid_switches > 0,
          "H3b arbiter records at least one switch away from stride");
}

/* H4: the Week 2 accounting invariant must survive the new predictor.
 *     issued == useful + wasted + still-outstanding, at end of trace. */
static void test_hybrid_preserves_accounting_invariant(void) {
    reset_all();
    long line = 5000;
    for (int i = 0; i < 40; i++) {
        access_address(line * LINE_SIZE);          line -= 8;
        access_address(line * LINE_SIZE);          line += 16;
    }
    CHECK(stat_prefetches_issued ==
          stat_prefetches_useful + stat_prefetches_wasted + outstanding_prefetches,
          "H4 issued == useful + wasted + outstanding (Week 2 invariant holds)");
}

/* H5: accuracy must stay bounded -- the Week 2 §3.2 bug class. */
static void test_hybrid_accuracy_bounded(void) {
    reset_all();
    for (int i = 0; i < 200; i++) access_address((long)i * LINE_SIZE);
    double accuracy = stat_prefetches_issued ?
        (double)stat_prefetches_useful / stat_prefetches_issued : 0.0;
    char msg[128];
    snprintf(msg, sizeof(msg), "H5 hybrid accuracy never exceeds 1.0 (got %.4f)", accuracy);
    CHECK(accuracy <= 1.0 + 1e-9, msg);
}

/* H6: the hybrid must never issue more than one prefetch per miss --
 *     otherwise it is quietly buying coverage with bandwidth and the
 *     comparison against stride is not like-for-like. */
static void test_hybrid_at_most_one_prefetch_per_miss(void) {
    reset_all();
    long line = 9000;
    for (int i = 0; i < 20; i++) { access_address(line * LINE_SIZE); line -= 8;
                                   access_address(line * LINE_SIZE); line += 16; }
    CHECK(stat_prefetches_issued <= stat_misses,
          "H6 hybrid issues at most one prefetch per miss");
}

int main(void) {
    test_hybrid_matches_stride_on_constant_stride();
    test_hybrid_prefers_stride_on_tie();
    test_hybrid_learns_cycling_pattern();
    test_hybrid_preserves_accounting_invariant();
    test_hybrid_accuracy_bounded();
    test_hybrid_at_most_one_prefetch_per_miss();
    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
