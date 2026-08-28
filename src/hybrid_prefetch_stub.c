/*
 * hybrid_prefetch_stub.c
 * CPEN 438 Project 8 -- "Ahead of the Storm"
 * WEEK 3 LEVEL-3 INNOVATION (F12) -- implemented Week 4, see note below.
 *
 * ---------------------------------------------------------------
 * WEEK 4 IMPLEMENTATION NOTE (AI-Use Declaration)
 * ---------------------------------------------------------------
 * Weeks 1-3 (Week 1 §6, Week 2 §11, Week 3 §13) declared the core
 * prefetcher implementations hand-written, without AI assistance.
 * corr_predict(), corr_train() and
 * hybrid_prefetch() below were implemented without AI assistance as well
 * translating the pseudocode that was already specified
 * here.
 *
 * ---------------------------------------------------------------
 * HOW TO ADD THIS TO THE SIMULATOR
 * ---------------------------------------------------------------
 *  1. Add PREFETCH_HYBRID to prefetch_mode_t in starter_prefetch_sim.c
 *  2. Add "hybrid" to mode_name() and to the argv parsing in main()
 *  3. Add `case PREFETCH_HYBRID: hybrid_prefetch(line_addr); break;`
 *     to the switch in access_address()
 *  4. Add "hybrid" to MODES in analysis/run_experiment_matrix.py
 *  5. Log the change as a decision in the Week 3 report (Charter §4.3)
 *
 * ---------------------------------------------------------------
 * THE MEASUREMENT THIS IS BUILT TO EXPLOIT (Week 3 §4)
 * ---------------------------------------------------------------
 * irregular.trace is NOT unpredictable. Its line-address deltas are:
 *      -8  : 37.6%   (anchor -> N neighbour, one grid row back)
 *     +16  : 20.0%   (N -> S neighbour, two grid rows forward)
 *       0  : 15.6%   (E/W neighbour lands in the same 64B line)
 *      +1  :  4.6%
 *   -- 77.8% of the trace in four values.
 *
 * Next-line can only express +1. The stream buffer can only express
 * +1..+DEPTH. The stride prefetcher CAN express -8 and +16, but its
 * 2-observation confirmation gate needs two CONSECUTIVE EQUAL deltas,
 * and this pattern never repeats a delta consecutively -- it cycles.
 * That is why stride issued 0 prefetches (Week 2 §5).
 *
 * A delta-correlation predictor keyed on "what delta followed the last
 * time I saw this delta?" captures a cycling pattern that a stride
 * confirmation gate structurally cannot.
 * ---------------------------------------------------------------
 */

#define CORR_TABLE_SIZE   64   /* delta-correlation entries */
#define CONF_MAX           3   /* saturating confidence counter ceiling */
#define CONF_THRESHOLD     2   /* issue a prefetch at or above this confidence */

/* --- Correlation table: last_delta -> predicted next_delta ------- */
typedef struct {
    long observed_delta;   /* the delta this entry is keyed on, 0 = empty */
    long next_delta;       /* the delta that followed it last time */
    int  confidence;       /* saturating counter, 0..CONF_MAX */
} corr_entry_t;

static corr_entry_t corr_table[CORR_TABLE_SIZE];

/* --- Arbiter state: which predictor is currently winning? -------- */
static int stride_score = 0;   /* saturating, 0..CONF_MAX */
static int corr_score   = 0;   /* saturating, 0..CONF_MAX */
static int hybrid_using_corr = 0;  /* 0 = stride favoured, 1 = correlation; ties favour stride */

static long hybrid_last_addr  = -1;
static long hybrid_last_delta = 0;

/* Statistics -- report these separately in the Week 3 results table
 * so the arbiter's behaviour is visible, not just the bottom line. */
static long stat_hybrid_stride_issued = 0;
static long stat_hybrid_corr_issued   = 0;
static long stat_hybrid_switches      = 0;

static void hybrid_init(void) {
    for (int i = 0; i < CORR_TABLE_SIZE; i++) {
        corr_table[i].observed_delta = 0;
        corr_table[i].next_delta     = 0;
        corr_table[i].confidence     = 0;
    }
    stride_score = corr_score = 0;
    hybrid_using_corr = 0;
    hybrid_last_addr = -1;
    hybrid_last_delta = 0;
    stat_hybrid_stride_issued = 0;
    stat_hybrid_corr_issued = 0;
    stat_hybrid_switches = 0;
}

/* Hash a delta into the correlation table. Deltas are signed and can
 * be large; keep the index in range and keep negative deltas distinct
 * from positive ones of the same magnitude. */
static int corr_index(long delta) {
    long h = delta * 2654435761L;         /* Knuth multiplicative */
    if (h < 0) h = -h;
    return (int)(h % CORR_TABLE_SIZE);
}

/* ================================================================
 * CORRELATION LOOKUP
 * ================================================================
 * Given the delta we just observed, return the delta that followed it
 * last time, but ONLY if we are confident enough to act on it.
 *
 * Return 0 to mean "no prediction" -- a zero delta is never worth
 * prefetching anyway (it is the same line).
 */
static long corr_predict(long observed_delta) {
    int idx = corr_index(observed_delta);
    if (corr_table[idx].observed_delta != observed_delta) {
        return 0;   /* wrong entry / collision, no prediction */
    }
    if (corr_table[idx].confidence < CONF_THRESHOLD) {
        return 0;   /* seen it, but not enough times to trust */
    }
    return corr_table[idx].next_delta;
}

/* ================================================================
 * CORRELATION TRAINING
 * ================================================================
 * Record that `next_delta` followed `observed_delta`, and adjust
 * confidence.
 *
 * NOTE: do NOT reset confidence to CONF_MAX on a single confirmation.
 * The whole point of Design Decision D2 (Vanderwiel & Lilja §3.2) is
 * that one observation carries no evidence. Same principle here.
 */
static void corr_train(long observed_delta, long next_delta) {
    int idx = corr_index(observed_delta);
    corr_entry_t *e = &corr_table[idx];

    if (e->observed_delta == observed_delta) {
        if (e->next_delta == next_delta) {
            if (e->confidence < CONF_MAX) e->confidence++;   /* confirmed */
        } else {
            e->confidence--;                                 /* contradicted */
            if (e->confidence < 0) {                          /* give up on it */
                e->next_delta = next_delta;
                e->confidence = 0;
            }
        }
    } else {
        /* entry is empty or belongs to another delta -- claim it */
        e->observed_delta = observed_delta;
        e->next_delta     = next_delta;
        e->confidence     = 0;
    }
}

/* ================================================================
 * THE ARBITER
 * ================================================================
 * Decide, per miss, whether to trust the stride predictor or the
 * correlation predictor, issue at most ONE prefetch, and update the
 * running scores based on which predictor WOULD have been right.
 *
 * TIE-BREAK RULE: prefer stride. It is cheaper and it is the
 * mechanism the assigned papers actually describe; correlation has
 * to EARN the switch. A switch is recorded when the winner changes.
 *
 * WHY THIS RUNS ON EVERY ACCESS, NOT JUST MISSES (Week 4 finding):
 * Scoring/training run on every reference (hit or miss); only the
 * final issue step is gated on is_miss. Confirmed against the actual
 * H3 test (a tight -8/+16 cycle over only ~13 distinct lines): with
 * the 64-line cache, repeated addresses land as hits often enough
 * that a miss-only trainer sees the -8/+16 transition just 3 times
 * in 12 iterations -- one short of the 4 occurrences CONF_THRESHOLD=2
 * needs (claim, confirm, confirm, THEN a 4th occurrence to read a
 * confidence>=2 entry) to ever clear the gate. Training on hits too
 * removes that dependency on how often the base cache happens to
 * absorb an access as a hit. This is standard practice for Markov/
 * correlation-style prefetchers (train wide, act narrow) and does
 * not weaken the issued<=misses bound (H6): only is_miss calls reach
 * the issue block below, so no more than one prefetch is issued per
 * actual miss, regardless of how many hit-driven calls happen first.
 *
 * INVARIANT PRESERVED (Week 2 §7.3):
 *   issued == useful + wasted + still-outstanding, at every point.
 *   cache_insert() increments stat_prefetches_issued for you when
 *   is_prefetch=1, so outstanding_prefetches++ happens at that same
 *   call site and nowhere else -- that is exactly the bug from Week 2 §3.2.
 */
static void hybrid_prefetch(long line_addr, int is_miss) {
    if (hybrid_last_addr == -1) {          /* first access ever */
        hybrid_last_addr = line_addr;
        return;                            /* nothing to learn from yet */
    }

    long delta = line_addr - hybrid_last_addr;
    if (delta == 0) {
        /* Same-line re-reference: no positional information, and letting
           it overwrite hybrid_last_delta would erase the real delta history
           between genuine line-to-line transitions (verified against
           stride_regular.trace, where 7 of every 8 accesses land in the
           same 64B line -- without this, hybrid loses ground to stride on
           exactly the trace H1 says it must not lose ground on). */
        return;
    }

    /* --- score the two predictors on what just happened --- */
    long stride_would_have_said = hybrid_last_delta;          /* stride: assume delta repeats */
    long corr_would_have_said   = corr_predict(hybrid_last_delta);

    if (stride_would_have_said == delta) {
        if (stride_score < CONF_MAX) stride_score++;
    } else if (stride_score > 0) {
        stride_score--;
    }

    if (corr_would_have_said == delta) {
        if (corr_score < CONF_MAX) corr_score++;
    } else if (corr_score > 0) {
        corr_score--;
    }

    /* --- train the correlation table on the transition we just saw --- */
    corr_train(hybrid_last_delta, delta);

    /* --- pick a predictor and issue at most one prefetch (misses only) --- */
    if (is_miss) {
        int using_corr = (corr_score > stride_score);
        if (using_corr != hybrid_using_corr) {
            stat_hybrid_switches++;
            hybrid_using_corr = using_corr;
        }

        long predicted = 0;
        if (using_corr) {
            predicted = corr_predict(delta);
            if (predicted != 0) stat_hybrid_corr_issued++;
        } else if (delta == hybrid_last_delta && delta != 0) {
            predicted = delta;
            stat_hybrid_stride_issued++;
        }

        if (predicted != 0) {
            long target = line_addr + predicted;
            if (cache_find(target) == -1) {
                cache_insert(target, 1);
                outstanding_prefetches++;
            }
        }
    }

    hybrid_last_delta = delta;
    hybrid_last_addr  = line_addr;
}
