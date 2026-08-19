/*
 * demo_prefetcher.c
 * CPEN 438 Project 8 -- "Ahead of the Storm"
 *
 * A COMPLETE, WORKING example of the simplest possible prefetcher:
 * next-line prefetching. Read this file top to bottom before you touch
 * starter_prefetch_sim.c -- every idea you need for Week 2 is already
 * demonstrated here in its simplest form.
 *
 * What next-line prefetching does:
 *   Every time the program misses on address A, we bring line(A) into
 *   the cache (that's a normal cache fill) AND we also go ahead and
 *   fetch the very next line, line(A + LINE_SIZE), speculatively --
 *   betting that the program will want it soon. If we're right, that
 *   future access becomes a hit instead of a miss. If we're wrong,
 *   we wasted bandwidth fetching something nobody used.
 *
 * Build:
 *   gcc -O2 -o demo_prefetcher demo_prefetcher.c
 * Run:
 *   ./demo_prefetcher some_trace_file.trace
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_SIZE      64      /* bytes per cache line */
#define NUM_LINES      64      /* number of lines the cache can hold */
#define MAX_ADDR_LEN   64

/* One cache line slot. A fully-associative cache with LRU replacement
 * is used here because it is the easiest kind to reason about by hand
 * -- there is no "index" bit math to worry about, just "is this
 * address's line already one of the NUM_LINES lines we're holding?" */
typedef struct {
    long line_addr;     /* which line (already divided by LINE_SIZE), -1 = empty */
    int  valid;
    int  was_prefetched;  /* 1 if this line got here via prefetch, not a demand access */
    int  prefetch_used;   /* 1 if a prefetched line was later actually accessed (a "useful" prefetch) */
    long last_used_tick;  /* for LRU eviction */
} cache_line_t;

static cache_line_t cache[NUM_LINES];
static long global_tick = 0;

/* Statistics we report at the end */
static long stat_accesses      = 0;
static long stat_hits          = 0;
static long stat_misses        = 0;
static long stat_prefetches_issued = 0;
static long stat_prefetches_useful = 0;
static long stat_prefetches_wasted = 0; /* evicted before ever being used */

static void cache_init(void) {
    for (int i = 0; i < NUM_LINES; i++) {
        cache[i].valid = 0;
        cache[i].line_addr = -1;
        cache[i].was_prefetched = 0;
        cache[i].prefetch_used = 0;
        cache[i].last_used_tick = 0;
    }
}

/* Find a line already in the cache. Returns the slot index, or -1 if not present. */
static int cache_find(long line_addr) {
    for (int i = 0; i < NUM_LINES; i++) {
        if (cache[i].valid && cache[i].line_addr == line_addr) return i;
    }
    return -1;
}

/* Pick a slot to evict: the empty one if there is one, else the least-recently-used one. */
static int cache_pick_victim(void) {
    for (int i = 0; i < NUM_LINES; i++) {
        if (!cache[i].valid) return i;
    }
    int victim = 0;
    long oldest = cache[0].last_used_tick;
    for (int i = 1; i < NUM_LINES; i++) {
        if (cache[i].last_used_tick < oldest) {
            oldest = cache[i].last_used_tick;
            victim = i;
        }
    }
    /* If we're evicting a prefetched line that was never touched, that
     * prefetch was wasted -- count it now, at the moment it's forced out. */
    if (cache[victim].valid && cache[victim].was_prefetched && !cache[victim].prefetch_used) {
        stat_prefetches_wasted++;
    }
    return victim;
}

/* Insert a line into the cache. is_prefetch=1 means "this fill was
 * speculative, not a real demand access". */
static void cache_insert(long line_addr, int is_prefetch) {
    if (cache_find(line_addr) != -1) return; /* already present, nothing to do */
    int slot = cache_pick_victim();
    cache[slot].valid = 1;
    cache[slot].line_addr = line_addr;
    cache[slot].was_prefetched = is_prefetch;
    cache[slot].prefetch_used = 0;
    cache[slot].last_used_tick = global_tick;
    if (is_prefetch) stat_prefetches_issued++;
}

/* This is the "next-line" logic: given the line we just missed on,
 * decide what (if anything) to prefetch. Here it is always exactly
 * one line ahead. */
static void next_line_prefetch(long missed_line_addr) {
    long prefetch_target = missed_line_addr + 1; /* the next line, in line units */
    if (cache_find(prefetch_target) == -1) {
        cache_insert(prefetch_target, /*is_prefetch=*/1);
    }
}

/* Process a single demand access (a real address the "program" touched). */
static void access_address(long byte_addr) {
    long line_addr = byte_addr / LINE_SIZE;
    global_tick++;
    stat_accesses++;

    int slot = cache_find(line_addr);
    if (slot != -1) {
        /* HIT. If this line got here via an earlier prefetch and this
         * is the first real use of it, credit that prefetch as useful. */
        stat_hits++;
        if (cache[slot].was_prefetched && !cache[slot].prefetch_used) {
            cache[slot].prefetch_used = 1;
            stat_prefetches_useful++;
        }
        cache[slot].last_used_tick = global_tick;
    } else {
        /* MISS. Bring the real line in (a normal demand fill), then
         * let the prefetcher decide what to fetch ahead of time. */
        stat_misses++;
        cache_insert(line_addr, /*is_prefetch=*/0);
        next_line_prefetch(line_addr);
    }
}

static void print_report(const char *trace_name) {
    double hit_rate = stat_accesses ? (double)stat_hits / stat_accesses : 0.0;
    double coverage = stat_misses ? (double)stat_prefetches_useful /
                       (stat_misses + stat_prefetches_useful) : 0.0;
    double accuracy = stat_prefetches_issued ?
                       (double)stat_prefetches_useful / stat_prefetches_issued : 0.0;

    printf("=== Next-Line Prefetcher Demo Results ===\n");
    printf("Trace file           : %s\n", trace_name);
    printf("Total accesses       : %ld\n", stat_accesses);
    printf("Hits                 : %ld\n", stat_hits);
    printf("Misses                : %ld\n", stat_misses);
    printf("Hit rate              : %.4f\n", hit_rate);
    printf("Prefetches issued     : %ld\n", stat_prefetches_issued);
    printf("Prefetches useful     : %ld\n", stat_prefetches_useful);
    printf("Prefetches wasted     : %ld  (evicted before ever used)\n", stat_prefetches_wasted);
    printf("Coverage              : %.4f  (fraction of would-be misses avoided)\n", coverage);
    printf("Accuracy              : %.4f  (fraction of prefetches that were useful)\n", accuracy);
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "Usage: %s <trace_file>\n", argv[0]);
        return 1;
    }

    FILE *f = fopen(argv[1], "r");
    if (!f) {
        fprintf(stderr, "Could not open trace file: %s\n", argv[1]);
        return 1;
    }

    cache_init();

    char line[MAX_ADDR_LEN];
    while (fgets(line, sizeof(line), f)) {
        long addr = strtol(line, NULL, 10);
        access_address(addr);
    }
    fclose(f);

    print_report(argv[1]);
    return 0;
}
