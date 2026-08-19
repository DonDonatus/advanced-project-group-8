/*
 * test_prefetch.c
 * Minimal unit tests for CPEN 438 Project 8, Week 2 gate.
 *
 * These test the SAME source file as prefetch_sim, by #including it,
 * so there is no risk of testing a stale copy of the logic.
 *
 * Build:
 *   gcc -O2 -Wall -DUNIT_TEST_BUILD -o test_prefetch test_prefetch.c
 * Run:
 *   ./test_prefetch
 */

#include <stdio.h>
#include <assert.h>

/* Rename main() in the simulator so it doesn't collide with this file's main() */
#define main sim_main_unused
#include "starter_prefetch_sim.c"
#undef main

static int failures = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); failures++; } \
    else { printf("PASS: %s\n", msg); } \
} while (0)

/* Reset all global state between tests so tests don't interfere with each other */
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
    g_mode = PREFETCH_NONE;
}

/* Test 1: stride must NOT fire after only one miss (needs two matching strides) */
static void test_stride_waits_for_confirmation(void) {
    reset_all();
    g_mode = PREFETCH_STRIDE;
    long issued_before = stat_prefetches_issued;
    /* first miss ever seen: line 0 -- no prior stride to compare against */
    access_address(0 * LINE_SIZE);
    CHECK(stat_prefetches_issued == issued_before,
          "stride issues nothing on the very first miss");
}

/* Test 2: stride DOES fire once the same stride repeats */
static void test_stride_fires_after_confirmation(void) {
    reset_all();
    g_mode = PREFETCH_STRIDE;
    access_address(0 * LINE_SIZE);   /* miss, line 0 */
    access_address(4 * LINE_SIZE);   /* miss, line 4, stride=4, no confirmation yet */
    long issued_before = stat_prefetches_issued;
    access_address(8 * LINE_SIZE);   /* miss, line 8, stride=4 again -- CONFIRMED */
    CHECK(stat_prefetches_issued == issued_before + 1,
          "stride issues exactly one prefetch once stride repeats twice");
}

/* Test 3: a used prefetch must never later be double-counted as wasted */
static void test_used_prefetch_not_later_wasted(void) {
    reset_all();
    g_mode = PREFETCH_NEXTLINE;
    access_address(0 * LINE_SIZE);   /* miss on line 0 -> issues prefetch for line 1 */
    access_address(1 * LINE_SIZE);   /* line 1: should HIT and be credited useful */
    CHECK(stat_prefetches_useful == 1, "prefetched line counted useful once consumed");

    /* Now flood the cache with NUM_LINES+5 unrelated misses to force eviction
       of everything, including the now-used line-1 entry. */
    long wasted_before = stat_prefetches_wasted;
    for (int i = 100; i < 100 + NUM_LINES + 5; i++) {
        access_address((long)i * LINE_SIZE);
    }
    CHECK(stat_prefetches_wasted == wasted_before,
          "a prefetch already marked used is never later counted as wasted on eviction");
}

/* Test 4: streambuf issued/useful/wasted stay internally consistent (accuracy <= 1.0) */
static void test_streambuf_accuracy_bounded(void) {
    reset_all();
    g_mode = PREFETCH_STREAMBUF;
    /* simulate a long sequential run: 30 consecutive addresses */
    for (int i = 0; i < 30; i++) {
        access_address((long)i * LINE_SIZE);
    }
    double accuracy = stat_prefetches_issued ?
        (double)stat_prefetches_useful / stat_prefetches_issued : 0.0;
    char msg[128];
    snprintf(msg, sizeof(msg), "streambuf accuracy never exceeds 1.0 (got %.4f)", accuracy);
    CHECK(accuracy <= 1.0 + 1e-9, msg);
}

/* Test 5: streambuf is checked on EVERY access, not just misses (the Jouppi detail) */
static void test_streambuf_checked_every_access(void) {
    reset_all();
    g_mode = PREFETCH_STREAMBUF;
    access_address(0 * LINE_SIZE);   /* miss -> fills stream buffer with lines 1..4 */
    long useful_before = stat_prefetches_useful;
    access_address(1 * LINE_SIZE);   /* line 1 is NOT yet in main cache, only in stream buf */
    CHECK(stat_prefetches_useful == useful_before + 1,
          "a line sitting only in the stream buffer (not main cache) still counts as a hit");
}

int main(void) {
    test_stride_waits_for_confirmation();
    test_stride_fires_after_confirmation();
    test_used_prefetch_not_later_wasted();
    test_streambuf_accuracy_bounded();
    test_streambuf_checked_every_access();

    printf("\n%d failure(s)\n", failures);
    return failures ? 1 : 0;
}
