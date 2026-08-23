/*
 * hybrid_prefetch_stub.c
 * CPEN 438 Project 8 -- "Ahead of the Storm"
 * WEEK 3 LEVEL-3 INNOVATION (F12) -- STUB, NOT AN IMPLEMENTATION.
 *
 * ---------------------------------------------------------------
 * WHY THIS FILE IS A STUB
 * ---------------------------------------------------------------
 * The team's standing AI-Use policy (Week 1 §6, Week 2 §11) is that
 * the core prefetcher implementations are written by the C/C++ Lead
 * without AI assistance, exactly as the Week 2 stride and stream-buffer
 * stubs were. The Level-3 innovation is Week 3's core deliverable, so
 * the same rule applies: the scaffolding, the pseudocode and the tests
 * are here, the prediction logic is not.
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
 * TODO 1 -- CORRELATION LOOKUP
 * ================================================================
 * Given the delta we just observed, return the delta that followed it
 * last time, but ONLY if we are confident enough to act on it.
 *
 * PSEUDOCODE:
 *   idx = corr_index(observed)
 *   if corr_table[idx].observed_delta != observed:
 *        return 0            // wrong entry / collision, no prediction
 *   if corr_table[idx].confidence < CONF_THRESHOLD:
 *        return 0            // seen it, but not enough times to trust
 *   return corr_table[idx].next_delta
 *
 * Return 0 to mean "no prediction" -- a zero delta is never worth
 * prefetching anyway (it is the same line).
 */
static long corr_predict(long observed_delta) {
    (void)observed_delta;
    return 0;   /* TODO: implement */
}

/* ================================================================
 * TODO 2 -- CORRELATION TRAINING
 * ================================================================
 * Record that `next` followed `observed`, and adjust confidence.
 *
 * PSEUDOCODE:
 *   idx = corr_index(observed)
 *   if corr_table[idx].observed_delta == observed:
 *        if corr_table[idx].next_delta == next:
 *             confidence = min(confidence + 1, CONF_MAX)   // confirmed
 *        else:
 *             confidence = confidence - 1                  // contradicted
 *             if confidence < 0:                           // give up on it
 *                  next_delta = next; confidence = 0       // retrain
 *   else:
 *        // entry is empty or belongs to another delta -- claim it
 *        observed_delta = observed; next_delta = next; confidence = 0
 *
 * NOTE: do NOT reset confidence to CONF_MAX on a single confirmation.
 * The whole point of Design Decision D2 (Vanderwiel & Lilja §3.2) is
 * that one observation carries no evidence. Same principle here.
 */
static void corr_train(long observed_delta, long next_delta) {
    (void)observed_delta; (void)next_delta;
    /* TODO: implement */
}

/* ================================================================
 * TODO 3 -- THE ARBITER
 * ================================================================
 * Decide, per miss, whether to trust the stride predictor or the
 * correlation predictor, issue at most ONE prefetch, and update the
 * running scores based on which predictor WOULD have been right.
 *
 * PSEUDOCODE:
 *   if hybrid_last_addr == -1:            // first miss ever
 *        hybrid_last_addr = missed; return          // nothing to learn from yet
 *
 *   delta = missed - hybrid_last_addr
 *
 *   // --- score the two predictors on what just happened ---
 *   // Whoever predicted `delta` correctly gains, the other loses.
 *   stride_would_have_said = hybrid_last_delta        // stride: assume delta repeats
 *   corr_would_have_said   = corr_predict(hybrid_last_delta)
 *
 *   if stride_would_have_said == delta: stride_score = min(stride_score+1, CONF_MAX)
 *   else if stride_score > 0:           stride_score--
 *
 *   if corr_would_have_said == delta:   corr_score = min(corr_score+1, CONF_MAX)
 *   else if corr_score > 0:             corr_score--
 *
 *   // --- train the correlation table on the transition we just saw ---
 *   corr_train(hybrid_last_delta, delta)
 *
 *   // --- pick a predictor and issue at most one prefetch ---
 *   // TIE-BREAK RULE: prefer stride. It is cheaper and it is the
 *   // mechanism the assigned papers actually describe; correlation
 *   // has to EARN the switch. Record a switch when the winner changes.
 *   predicted = 0
 *   if corr_score > stride_score:
 *        predicted = corr_predict(delta);  if predicted: stat_hybrid_corr_issued++
 *   else:
 *        if delta == hybrid_last_delta and delta != 0:
 *             predicted = delta;           if predicted: stat_hybrid_stride_issued++
 *
 *   if predicted != 0:
 *        target = missed + predicted
 *        if cache_find(target) == -1:
 *             cache_insert(target, 1)
 *             outstanding_prefetches++
 *
 *   hybrid_last_delta = delta
 *   hybrid_last_addr  = missed
 *
 * INVARIANT TO PRESERVE (Week 2 §7.3):
 *   issued == useful + wasted + still-outstanding, at every point.
 *   cache_insert() increments stat_prefetches_issued for you when
 *   is_prefetch=1, so increment outstanding_prefetches at the same
 *   point and nowhere else -- that is exactly the bug from Week 2 §3.2.
 */
static void hybrid_prefetch(long missed_line_addr) {
    (void)missed_line_addr;
    /* TODO: implement */
}
