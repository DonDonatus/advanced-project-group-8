/*
 * starter_prefetch_sim.c
 * CPEN 438 Project 8 -- "Ahead of the Storm"
 *
 * Week 2 -- stride and stream-buffer prefetchers implemented.
 *
 * Build:
 *   gcc -O2 -Wall -o prefetch_sim starter_prefetch_sim.c
 * Run:
 *   ./prefetch_sim <trace_file> <mode>
 *   where <mode> is one of: none | nextline | stride | streambuf | hybrid
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
    PREFETCH_STREAMBUF,
    PREFETCH_HYBRID
} prefetch_mode_t;

/* ---------------------------------------------------------------- */
/* Base cache -- unchanged from the starter                          */
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
static long outstanding_prefetches = 0;

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
/* MLP tracker -- unchanged from the starter                         */
/* ---------------------------------------------------------------- */

static double mlp_sum = 0.0;
static long mlp_samples = 0;

static void mlp_sample(void) {
    mlp_sum += (double)outstanding_prefetches;
    mlp_samples++;
}

/* ---------------------------------------------------------------- */
/* PREFETCH_NEXTLINE -- unchanged reference implementation           */
/* ---------------------------------------------------------------- */

static void next_line_prefetch(long missed_line_addr) {
    long target = missed_line_addr + 1;
    if (cache_find(target) == -1) {
        cache_insert(target, 1);
        outstanding_prefetches++;
    }
}

/* ---------------------------------------------------------------- */
/* PREFETCH_STRIDE -- IMPLEMENTED                                    */
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

/* ---------------------------------------------------------------- */
/* PREFETCH_STREAMBUF -- IMPLEMENTED (Jouppi, ISCA '90)               */
/* ---------------------------------------------------------------- */

static long stream_buf[STREAMBUF_DEPTH];
static int  stream_buf_valid[STREAMBUF_DEPTH];
static long stream_buf_next_fetch = -1;

static void streambuf_prefetch_on_miss(long missed_line_addr) {
    /* Refilling: any old slots still marked valid here were never used --
       count them as wasted so accuracy/MLP bookkeeping stays honest. */
    for (int i = 0; i < STREAMBUF_DEPTH; i++) {
        if (stream_buf_valid[i]) {
            stat_prefetches_wasted++;
            if (outstanding_prefetches > 0) outstanding_prefetches--;
        }
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
                stat_prefetches_issued++;   /* keeps issued/useful/wasted all counting the same events */
            }
            return 1;
        }
    }
    return 0;
}

/* ---------------------------------------------------------------- */
/* PREFETCH_HYBRID -- correlation + stride arbiter                   */
/* Included here (not near the top) so it can call cache_find(),     */
/* cache_insert() and use outstanding_prefetches, all defined above. */
/* ---------------------------------------------------------------- */

#include "hybrid_prefetch_stub.c"

/* ---------------------------------------------------------------- */
/* Access dispatcher -- unchanged from the starter                   */
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
        /* Hybrid trains its correlation table on every reference, hit or
           miss (see the WHY note above hybrid_prefetch()); it only issues
           a prefetch when is_miss is true, so this never affects H6. */
        if (g_mode == PREFETCH_HYBRID) hybrid_prefetch(line_addr, 0);
    } else {
        stat_misses++;
        cache_insert(line_addr, 0);

        switch (g_mode) {
            case PREFETCH_NEXTLINE:  next_line_prefetch(line_addr); break;
            case PREFETCH_STRIDE:    stride_prefetch(line_addr); break;
            case PREFETCH_STREAMBUF: streambuf_prefetch_on_miss(line_addr); break;
            case PREFETCH_HYBRID:    hybrid_prefetch(line_addr, 1); break;
            default: break;
        }
    }
    mlp_sample();
}

/* ---------------------------------------------------------------- */
/* Reporting -- unchanged from the starter                           */
/* ---------------------------------------------------------------- */

static const char *mode_name(prefetch_mode_t m) {
    switch (m) {
        case PREFETCH_NONE:      return "none";
        case PREFETCH_NEXTLINE:  return "nextline";
        case PREFETCH_STRIDE:    return "stride";
        case PREFETCH_STREAMBUF: return "streambuf";
        case PREFETCH_HYBRID:    return "hybrid";
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
        fprintf(stderr, "Usage: %s <trace_file> <none|nextline|stride|streambuf|hybrid>\n", argv[0]);
        return 1;
    }

    if      (strcmp(argv[2], "none") == 0)      g_mode = PREFETCH_NONE;
    else if (strcmp(argv[2], "nextline") == 0)  g_mode = PREFETCH_NEXTLINE;
    else if (strcmp(argv[2], "stride") == 0)    g_mode = PREFETCH_STRIDE;
    else if (strcmp(argv[2], "streambuf") == 0) g_mode = PREFETCH_STREAMBUF;
    else if (strcmp(argv[2], "hybrid") == 0)    g_mode = PREFETCH_HYBRID;
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
    hybrid_init();

    char line[MAX_ADDR_LEN];
    while (fgets(line, sizeof(line), f)) {
        long addr = strtol(line, NULL, 10);
        access_address(addr);
    }
    fclose(f);

    print_report(argv[1]);
    return 0;
}