/*
 * starter_prefetch_sim.c
 * CPEN 438 Project 8 -- "Ahead of the Storm"
 *
 * Week 2 starter code. This file already contains, fully working:
 *   - a base cache (same design as demo_prefetcher.c)
 *   - an MLP (Memory-Level Parallelism) tracker
 *   - the next-line prefetcher (PREFETCH_NEXTLINE), copied from the demo
 *
 * Your job (the two TODO sections, search for "TODO"):
 *   1. PREFETCH_STRIDE      -- a stride-detecting prefetcher
 *   2. PREFETCH_STREAMBUF   -- a Jouppi-style stream-buffer prefetcher
 *
 * Build:
 *   gcc -O2 -o prefetch_sim starter_prefetch_sim.c
 * Run:
 *   ./prefetch_sim <trace_file> <mode>
 *   where <mode> is one of: none | nextline | stride | streambuf
 *
 * Example:
 *   ./prefetch_sim rainfall.trace stride
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINE_SIZE      64
#define NUM_LINES      64
#define MAX_ADDR_LEN   64
#define STREAMBUF_DEPTH 4   /* how many lines ahead the stream buffer prefetches */

typedef enum {
    PREFETCH_NONE = 0,
    PREFETCH_NEXTLINE,
    PREFETCH_STRIDE,
    PREFETCH_STREAMBUF
} prefetch_mode_t;

/* ---------------------------------------------------------------- */
/* Base cache -- do not need to modify this section                 */
/* ---------------------------------------------------------------- */

typedef struct {
    long line_addr;
    int  valid;
    int  was_prefetched;
    int  prefetch_used;
    long last_used_tick;
} cache_line_t;

static cache_line_t cache[NUM_LINES];
static long global_tick = 0;
static long outstanding_prefetches = 0;  /* declared here so cache_pick_victim() can use it */

static long stat_accesses          = 0;
static long stat_hits              = 0;
static long stat_misses            = 0;
static long stat_prefetches_issued = 0;
static long stat_prefetches_useful = 0;
static long stat_prefetches_wasted = 0;

static void cache_init(void) {
    for (int i = 0; i < NUM_LINES; i++) {
        cache[i].valid = 0;
        cache[i].line_addr = -1;
        cache[i].was_prefetched = 0;
        cache[i].prefetch_used = 0;
        cache[i].last_used_tick = 0;
    }
}

static int cache_find(long line_addr) {
    for (int i = 0; i < NUM_LINES; i++) {
        if (cache[i].valid && cache[i].line_addr == line_addr) return i;
    }
    return -1;
}

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
    if (cache[victim].valid && cache[victim].was_prefetched && !cache[victim].prefetch_used) {
        stat_prefetches_wasted++;
        /* This prefetch is leaving the cache without ever being used --
         * it stops being "outstanding" now, whether it was useful or not. */
        if (outstanding_prefetches > 0) outstanding_prefetches--;
    }
    return victim;
}

static void cache_insert(long line_addr, int is_prefetch) {
    if (cache_find(line_addr) != -1) return;
    int slot = cache_pick_victim();
    cache[slot].valid = 1;
    cache[slot].line_addr = line_addr;
    cache[slot].was_prefetched = is_prefetch;
    cache[slot].prefetch_used = 0;
    cache[slot].last_used_tick = global_tick;
    if (is_prefetch) stat_prefetches_issued++;
}

/* ---------------------------------------------------------------- */
/* MLP (Memory-Level Parallelism) tracker -- do not need to modify   */
/*                                                                    */
/* Simplified model: every prefetch we issue that has NOT yet been   */
/* consumed by a demand access counts as one "outstanding" memory     */
/* request running in the background while the program keeps going.  */
/* MLP = the average number of such outstanding requests, sampled     */
/* once per access. Higher MLP means prefetching is successfully      */
/* overlapping multiple memory requests instead of serialising them.  */
/* ---------------------------------------------------------------- */

static double mlp_sum = 0.0;
static long mlp_samples = 0;

static void mlp_sample(void) {
    mlp_sum += (double)outstanding_prefetches;
    mlp_samples++;
}

/* ---------------------------------------------------------------- */
/* PREFETCH_NEXTLINE -- fully working, provided as your reference    */
/* ---------------------------------------------------------------- */

static void next_line_prefetch(long missed_line_addr) {
    long target = missed_line_addr + 1;
    if (cache_find(target) == -1) {
        cache_insert(target, 1);
        outstanding_prefetches++;
    }
}

/* ---------------------------------------------------------------- */
/* PREFETCH_STRIDE -- TODO: implement this                           */
/*                                                                    */
/* Idea: keep track of the last TWO miss addresses. If the distance   */
/* (stride) between them is the same as the distance between the      */
/* previous pair, we've detected a repeating stride -- prefetch        */
/* (current_miss + stride).                                            */
/*                                                                    */
/* Why wait for confirmation instead of guessing after one miss?      */
/* Because a single miss tells you nothing about direction or step    */
/* size -- you need at least two data points to compute a stride at   */
/* all, and a third to gain any confidence it's a *repeating* pattern */
/* rather than a coincidence.                                          */
/*                                                                    */
/* Suggested state to keep (module-level, like the globals above):    */
/*   static long last_miss_addr = -1;                                 */
/*   static long last_stride = 0;                                     */
/*                                                                    */
/* Suggested logic inside stride_prefetch(long missed_line_addr):     */
/*   1. if last_miss_addr != -1:                                      */
/*        long stride = missed_line_addr - last_miss_addr;            */
/*        if stride == last_stride and stride != 0:                   */
/*            // confirmed repeating stride -- prefetch ahead          */
/*            long target = missed_line_addr + stride;                 */
/*            if (cache_find(target) == -1) {                          */
/*                cache_insert(target, 1);                              */
/*                outstanding_prefetches++;                             */
/*            }                                                         */
/*        last_stride = stride;                                        */
/*   2. last_miss_addr = missed_line_addr;                             */
/*                                                                    */
/* Verify this against your Week 1 hand-trace before moving on.       */
/* ---------------------------------------------------------------- */

static long last_miss_addr = -1;
static long last_stride = 0;

static void stride_prefetch(long missed_line_addr) {
    if (last_miss_addr != -1) {
        long stride = missed_line_addr - last_miss_addr;
        if (stride == last_stride && stride != 0) {
            long target = missed_line_addr + stride;
            if (cache_find(target) == -1) {
                cache_insert(target, 1);
                outstanding_prefetches++;
            }
        }
        last_stride = stride;
    }
    last_miss_addr = missed_line_addr;
}

static void stride_prefetch(long missed_line_addr) {
    (void)missed_line_addr;
    /* TODO: replace this stub with the logic described above */
}

/* ---------------------------------------------------------------- */
/* PREFETCH_STREAMBUF -- TODO: implement this (Jouppi, ISCA '90)     */
/*                                                                    */
/* Idea: keep a SEPARATE small FIFO queue of lines, distinct from     */
/* the main cache. On a miss, start filling the stream buffer with    */
/* the next STREAMBUF_DEPTH sequential lines ahead of the miss.       */
/* On EVERY access (not just misses!) check the stream buffer FIRST:  */
/* if the requested line is sitting in the stream buffer, that is a   */
/* stream-buffer hit -- move that line into the main cache, and shift */
/* the stream buffer forward by fetching one more line at the far end */
/* to keep it full. This "check on every access, not just misses" is  */
/* the detail teams most often get wrong -- a stream buffer that only */
/* gets checked on cache misses will silently perform worse than it   */
/* should.                                                             */
/*                                                                    */
/* Suggested state:                                                    */
/*   static long stream_buf[STREAMBUF_DEPTH];                          */
/*   static int  stream_buf_valid[STREAMBUF_DEPTH];                    */
/*   static long stream_buf_next_fetch = -1; // next line to top up   */
/*                                                                    */
/* Suggested logic, called for EVERY access (not just misses), before */
/* the normal cache lookup:                                            */
/*   1. Search stream_buf[] for line_addr.                             */
/*      If found: remove it from the stream buffer, insert it into    */
/*      the main cache as a prefetch hit (is_prefetch=1, and credit    */
/*      it useful immediately since it's being used right now), then  */
/*      slide the buffer: fetch stream_buf_next_fetch into the freed   */
/*      slot and increment stream_buf_next_fetch.                      */
/*   2. If it was a genuine MISS in both cache and stream buffer:      */
/*      refill the whole stream buffer starting at line_addr + 1,      */
/*      for STREAMBUF_DEPTH lines, and set stream_buf_next_fetch       */
/*      to line_addr + 1 + STREAMBUF_DEPTH.                             */
/*                                                                    */
/* Verify this against your Week 1 hand-trace before moving on.       */
/* ---------------------------------------------------------------- */

static long stream_buf[STREAMBUF_DEPTH];
static int  stream_buf_valid[STREAMBUF_DEPTH];
static long stream_buf_next_fetch = -1;

static void streambuf_prefetch_on_miss(long missed_line_addr) {
    for (int i = 0; i < STREAMBUF_DEPTH; i++) {
        stream_buf[i] = missed_line_addr + 1 + i;
        stream_buf_valid[i] = 1;
    }
    stream_buf_next_fetch = missed_line_addr + 1 + STREAMBUF_DEPTH;
    stat_prefetches_issued += STREAMBUF_DEPTH;
    outstanding_prefetches += STREAMBUF_DEPTH;
}

static int streambuf_check(long line_addr) {
    for (int i = 0; i < STREAMBUF_DEPTH; i++) {
        if (stream_buf_valid[i] && stream_buf[i] == line_addr) {
            cache_insert(line_addr, 1);
            int slot = cache_find(line_addr);
            cache[slot].prefetch_used = 1;
            cache[slot].last_used_tick = global_tick;
            if (outstanding_prefetches > 0) outstanding_prefetches--;

            stream_buf_valid[i] = 0;
            if (stream_buf_next_fetch != -1) {
                stream_buf[i] = stream_buf_next_fetch;
                stream_buf_valid[i] = 1;
                stream_buf_next_fetch++;
                outstanding_prefetches++;
            }
            return 1;
        }
    }
    return 0;
}



static void streambuf_prefetch_on_miss(long missed_line_addr) {
    (void)missed_line_addr;
    /* TODO: refill logic described above */
}

static int streambuf_check(long line_addr) {
    (void)line_addr;
    /* TODO: search logic described above.
       Return 1 if found (and handle sliding the buffer forward),
       return 0 if not found. */
    return 0;
}

/* ---------------------------------------------------------------- */
/* Access dispatcher                                                  */
/* ---------------------------------------------------------------- */

static prefetch_mode_t g_mode = PREFETCH_NONE;

static void access_address(long byte_addr) {
    long line_addr = byte_addr / LINE_SIZE;
    global_tick++;
    stat_accesses++;

    if (g_mode == PREFETCH_STREAMBUF) {
        if (streambuf_check(line_addr)) {
            stat_hits++;
            stat_prefetches_useful++;
            mlp_sample();
            return;
        }
    }

    int slot = cache_find(line_addr);
    if (slot != -1) {
        stat_hits++;
        if (cache[slot].was_prefetched && !cache[slot].prefetch_used) {
            cache[slot].prefetch_used = 1;
            stat_prefetches_useful++;
            if (outstanding_prefetches > 0) outstanding_prefetches--;
        }
        cache[slot].last_used_tick = global_tick;
    } else {
        stat_misses++;
        cache_insert(line_addr, 0);

        switch (g_mode) {
            case PREFETCH_NEXTLINE:  next_line_prefetch(line_addr); break;
            case PREFETCH_STRIDE:    stride_prefetch(line_addr); break;
            case PREFETCH_STREAMBUF: streambuf_prefetch_on_miss(line_addr); break;
            default: break; /* PREFETCH_NONE: do nothing extra */
        }
    }
    mlp_sample();
}

/* ---------------------------------------------------------------- */
/* Reporting                                                           */
/* ---------------------------------------------------------------- */

static const char *mode_name(prefetch_mode_t m) {
    switch (m) {
        case PREFETCH_NONE:      return "none";
        case PREFETCH_NEXTLINE:  return "nextline";
        case PREFETCH_STRIDE:    return "stride";
        case PREFETCH_STREAMBUF: return "streambuf";
    }
    return "?";
}

static void print_report(const char *trace_name) {
    double hit_rate = stat_accesses ? (double)stat_hits / stat_accesses : 0.0;
    double coverage = (stat_misses + stat_prefetches_useful) ?
        (double)stat_prefetches_useful / (stat_misses + stat_prefetches_useful) : 0.0;
    double accuracy = stat_prefetches_issued ?
        (double)stat_prefetches_useful / stat_prefetches_issued : 0.0;
    double mlp = mlp_samples ? mlp_sum / mlp_samples : 0.0;

    printf("=== Prefetch Simulation Results ===\n");
    printf("Trace file           : %s\n", trace_name);
    printf("Prefetch mode         : %s\n", mode_name(g_mode));
    printf("Total accesses        : %ld\n", stat_accesses);
    printf("Hits                  : %ld\n", stat_hits);
    printf("Misses                : %ld\n", stat_misses);
    printf("Hit rate               : %.4f\n", hit_rate);
    printf("Prefetches issued      : %ld\n", stat_prefetches_issued);
    printf("Prefetches useful      : %ld\n", stat_prefetches_useful);
    printf("Prefetches wasted      : %ld\n", stat_prefetches_wasted);
    printf("Coverage                : %.4f\n", coverage);
    printf("Accuracy                : %.4f\n", accuracy);
    printf("MLP (avg outstanding)   : %.4f\n", mlp);
}

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <trace_file> <none|nextline|stride|streambuf>\n", argv[0]);
        return 1;
    }

    if      (strcmp(argv[2], "none") == 0)      g_mode = PREFETCH_NONE;
    else if (strcmp(argv[2], "nextline") == 0)  g_mode = PREFETCH_NEXTLINE;
    else if (strcmp(argv[2], "stride") == 0)    g_mode = PREFETCH_STRIDE;
    else if (strcmp(argv[2], "streambuf") == 0) g_mode = PREFETCH_STREAMBUF;
    else {
        fprintf(stderr, "Unknown mode '%s'\n", argv[2]);
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
