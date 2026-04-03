/*
 * @file test_debounce.c
 * @brief Unit tests for debounce.
 */

#include "debounce.h"
#include <stdio.h>
#include <stdlib.h>

#define TEST_ASSERT(expr)                                                      \
        do {                                                                   \
                if (!(expr)) {                                                 \
                        fprintf(stderr, "FAIL  %s:%d  %s\n", __FILE__,         \
                                __LINE__, #expr);                              \
                        exit(EXIT_FAILURE);                                    \
                }                                                              \
        } while (0)

#define TEST_PASS(name) fprintf(stdout, "PASS  %s\n", (name))

#define TEST_CASE(name)                                                        \
        static void name(void);                                                \
        static void name(void)

/* ── debounce_init ───────────────────────────────────────────────────────── */

/* Init returns true, sets trip, enables, and zeroes all other fields */
TEST_CASE(test_init_sets_trip_and_defaults)
{
        struct debounce db;
        bool ok = debounce_init(&db, 5u);

        TEST_ASSERT(ok == true);
        TEST_ASSERT(db.trip == 5u);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == false);
        TEST_ASSERT(db.enabled == true);
}

/* Init with NULL db returns false and must not crash */
TEST_CASE(test_init_null_db)
{
        bool ok = debounce_init(NULL, 5u);
        TEST_ASSERT(ok == false);
}

/* Init with trip=0 returns false and does not modify the object */
TEST_CASE(test_init_zero_trip_returns_false)
{
        struct debounce db;
        /* Poison the struct to verify it is not modified on failure */
        db.trip = 99u;
        db.counter = 99u;
        db.output = true;
        db.latch = true;
        db.enabled = true;

        bool ok = debounce_init(&db, 0u);

        TEST_ASSERT(ok == false);
        TEST_ASSERT(db.trip == 99u);    /* unchanged */
        TEST_ASSERT(db.counter == 99u); /* unchanged */
}

/* ── debounce_update ─────────────────────────────────────────────────────── */

/* Counter increments on each asserted tick; output is false before trip */
TEST_CASE(test_update_counter_increments_before_trip)
{
        struct debounce db;
        debounce_init(&db, 4u);

        for (uint16_t i = 1u; i < 4u; i++) {
                bool result = debounce_update(&db, true);
                TEST_ASSERT(result == false);
                TEST_ASSERT(db.counter == i);
                TEST_ASSERT(db.output == false);
        }
}

/* Output asserts on exactly the trip-th consecutive asserted tick */
TEST_CASE(test_update_output_asserts_at_trip)
{
        struct debounce db;
        const uint16_t trip = 3u;
        debounce_init(&db, trip);

        bool result = false;
        for (uint16_t i = 0u; i < trip; i++) {
                result = debounce_update(&db, true);
        }

        TEST_ASSERT(result == true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.counter == trip);
}

/* A single de-asserted tick resets counter and output; latch persists */
TEST_CASE(test_update_deassert_clears_output_preserves_latch)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);

        bool result = debounce_update(&db, false);
        TEST_ASSERT(result == false);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.latch == true); /* sticky: NOT cleared */
}

/* trip=1: first asserted tick must assert the output immediately */
TEST_CASE(test_update_trip_one_asserts_on_first_tick)
{
        struct debounce db;
        debounce_init(&db, 1u);

        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.counter == 1u);
}

/* Counter saturates at trip; hundreds of extra ticks must not overflow it */
TEST_CASE(test_update_counter_saturates_at_trip)
{
        struct debounce db;
        const uint16_t trip = 3u;
        debounce_init(&db, trip);

        for (int i = 0; i < 200; i++) {
                debounce_update(&db, true);
        }

        TEST_ASSERT(db.counter == trip);
        TEST_ASSERT(db.output == true);
}

/* trip=UINT16_MAX: counter advances correctly near the upper bound and then
 * saturates without overflowing */
TEST_CASE(test_update_counter_saturates_at_uint16_max_trip)
{
        struct debounce db;
        const uint16_t trip = UINT16_MAX;

        TEST_ASSERT(debounce_init(&db, trip) == true);

        db.counter = (uint16_t)(trip - 1u);

        TEST_ASSERT(debounce_update(&db, true) == true);
        TEST_ASSERT(db.counter == trip);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);

        TEST_ASSERT(debounce_update(&db, true) == true);
        TEST_ASSERT(db.counter == trip);
        TEST_ASSERT(db.output == true);
}

/* After output clears the debouncer re-asserts on a fresh run */
TEST_CASE(test_update_reasserts_after_deassert)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);

        debounce_update(&db, false);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.counter == 0u);

        debounce_update(&db, true);
        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == true);
        TEST_ASSERT(db.output == true);
}

/* Partial count then de-assert fully resets counter (no hysteresis) */
TEST_CASE(test_update_partial_count_then_deassert_resets_counter)
{
        struct debounce db;
        debounce_init(&db, 5u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.counter == 2u);

        debounce_update(&db, false);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
}

/* NULL db returns false without crashing */
TEST_CASE(test_update_null_db_returns_false)
{
        bool result = debounce_update(NULL, true);
        TEST_ASSERT(result == false);
}

/* trip=0 is now rejected by init; update also guards safely if trip is
 * somehow zero (e.g. via direct field write), returning false */
TEST_CASE(test_update_zero_trip_returns_false)
{
        struct debounce db;
        (void)debounce_init(&db, 1u); /* valid init */
        db.trip = 0u;                 /* corrupt trip directly */

        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == false);
}

/* When not enabled, update is a no-op: state is unchanged, returns false */
TEST_CASE(test_update_noop_when_disabled)
{
        struct debounce db;
        debounce_init(&db, 2u);
        debounce_disable(&db);

        /* Calling update with cond=true while disabled must not advance state
         */
        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == false);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
}

/* ── sticky latch behaviour ──────────────────────────────────────────────── */

/* Latch is set when output first goes true and stays set after deassert */
TEST_CASE(test_latch_set_on_output_and_persists_after_deassert)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(debounce_is_latched(&db) == true);

        debounce_update(&db, false); /* output clears, latch must not */
        TEST_ASSERT(debounce_is_latched(&db) == true);
}

/* debounce_clear_latch clears only the latch; other state is untouched */
TEST_CASE(test_clear_latch_clears_only_latch)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true); /* latch set */
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);

        debounce_clear_latch(&db);

        TEST_ASSERT(db.latch == false);
        TEST_ASSERT(db.output == true);  /* unchanged */
        TEST_ASSERT(db.counter == 1u);   /* unchanged */
        TEST_ASSERT(db.enabled == true); /* unchanged */
}

/* Latch is re-set on the next trip after clear_latch while output is active */
TEST_CASE(test_latch_resets_after_clear_then_reasserts)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true);
        TEST_ASSERT(db.latch == true);

        debounce_clear_latch(&db);
        TEST_ASSERT(db.latch == false);

        /* Next update with cond=true re-sets the latch */
        debounce_update(&db, true);
        TEST_ASSERT(db.latch == true);
}

/* NULL db to clear_latch must not crash */
TEST_CASE(test_clear_latch_null_db)
{
        debounce_clear_latch(NULL); /* Must not crash */
}

/* ── debounce_reset ──────────────────────────────────────────────────────── */

/* Reset clears counter, output, latch; preserves trip and enabled */
TEST_CASE(test_reset_clears_debounce_state_preserves_config)
{
        struct debounce db;
        debounce_init(&db, 3u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);

        debounce_reset(&db);

        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == false);
        TEST_ASSERT(db.trip == 3u);      /* preserved */
        TEST_ASSERT(db.enabled == true); /* preserved */
}

/* NULL db to reset must not crash */
TEST_CASE(test_reset_null_db)
{
        debounce_reset(NULL); /* Must not crash */
}

/* ── debounce_enable / debounce_disable / debounce_is_enabled ────────────── */

/* enable sets enabled without modifying other state */
TEST_CASE(test_enable_sets_enabled_only)
{
        struct debounce db;
        debounce_init(&db, 2u);
        debounce_disable(&db);
        TEST_ASSERT(db.enabled == false);

        debounce_enable(&db);

        TEST_ASSERT(db.enabled == true);
        TEST_ASSERT(db.counter == 0u);   /* unchanged by enable */
        TEST_ASSERT(db.output == false); /* unchanged by enable */
}

/* disable clears enabled, counter, and output; latch is preserved */
TEST_CASE(test_disable_clears_gate_and_transient_state_preserves_latch)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);
        TEST_ASSERT(db.counter == 2u);

        debounce_disable(&db);

        TEST_ASSERT(db.enabled == false);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == true); /* sticky: preserved across disable */
}

/* is_enabled correctly reflects the enabled field; NULL returns false */
TEST_CASE(test_is_enabled_reflects_state)
{
        struct debounce db;
        debounce_init(&db, 2u);

        TEST_ASSERT(debounce_is_enabled(&db) == true);
        TEST_ASSERT(debounce_is_enabled(NULL) == false);

        debounce_disable(&db);
        TEST_ASSERT(debounce_is_enabled(&db) == false);

        debounce_enable(&db);
        TEST_ASSERT(debounce_is_enabled(&db) == true);
}

/* disable then enable gives a clean slate ready for fresh counting */
TEST_CASE(test_disable_then_enable_gives_clean_state)
{
        struct debounce db;
        debounce_init(&db, 3u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.counter == 2u);

        debounce_disable(&db); /* clears counter */
        debounce_enable(&db);

        /* Counter starts from zero again */
        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == false);
        TEST_ASSERT(db.counter == 1u);
}

/* ── debounce_is_active / debounce_get_counter / debounce_get_trip ───────── */

/* is_active returns the output field; NULL returns false */
TEST_CASE(test_is_active_reflects_output)
{
        struct debounce db;
        debounce_init(&db, 1u);

        TEST_ASSERT(debounce_is_active(&db) == false);
        TEST_ASSERT(debounce_is_active(NULL) == false);

        debounce_update(&db, true);
        TEST_ASSERT(debounce_is_active(&db) == true);

        debounce_update(&db, false);
        TEST_ASSERT(debounce_is_active(&db) == false);
}

/* get_counter returns the current counter; NULL returns 0 */
TEST_CASE(test_get_counter_returns_counter)
{
        struct debounce db;
        debounce_init(&db, 5u);

        TEST_ASSERT(debounce_get_counter(&db) == 0u);
        TEST_ASSERT(debounce_get_counter(NULL) == 0u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(debounce_get_counter(&db) == 2u);
}

/* get_trip returns the configured trip threshold; NULL returns 0 */
TEST_CASE(test_get_trip_returns_trip)
{
        struct debounce db;
        debounce_init(&db, 7u);

        TEST_ASSERT(debounce_get_trip(&db) == 7u);
        TEST_ASSERT(debounce_get_trip(NULL) == 0u);
}

/* ── robustness & edge-case tests ───────────────────────────────────────── */

/* Rapid toggling (true/false alternation) faster than trip must never assert */
TEST_CASE(test_update_rapid_toggling_never_asserts)
{
        struct debounce db;
        debounce_init(&db, 3u);

        for (int i = 0; i < 100; i++) {
                bool result = debounce_update(&db, true);
                TEST_ASSERT(result == false);
                TEST_ASSERT(db.counter == 1u);

                result = debounce_update(&db, false);
                TEST_ASSERT(result == false);
                TEST_ASSERT(db.counter == 0u);
        }

        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == false);
}

/* Re-init on an already-active object cleanly resets all state */
TEST_CASE(test_reinit_on_live_object)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);
        TEST_ASSERT(db.counter == 2u);

        bool ok = debounce_init(&db, 10u);
        TEST_ASSERT(ok == true);
        TEST_ASSERT(db.trip == 10u);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == false);
        TEST_ASSERT(db.enabled == true);
}

/* Double disable is idempotent and does not corrupt state */
TEST_CASE(test_double_disable_is_idempotent)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.latch == true);

        debounce_disable(&db);
        debounce_disable(&db);

        TEST_ASSERT(db.enabled == false);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == true); /* preserved across both disables */
}

/* Double enable is idempotent and does not corrupt state */
TEST_CASE(test_double_enable_is_idempotent)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        TEST_ASSERT(db.counter == 1u);

        debounce_enable(&db);
        debounce_enable(&db);

        TEST_ASSERT(db.enabled == true);
        TEST_ASSERT(db.counter == 1u); /* not disturbed by redundant enables */
        TEST_ASSERT(db.output == false);
}

/* Clearing an already-clear latch is a no-op */
TEST_CASE(test_clear_latch_when_already_clear_is_noop)
{
        struct debounce db;
        debounce_init(&db, 2u);

        TEST_ASSERT(db.latch == false);

        debounce_clear_latch(&db);

        TEST_ASSERT(db.latch == false);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.enabled == true);
}

/* Reset while disabled does not re-enable; preserves enabled=false */
TEST_CASE(test_reset_while_disabled_preserves_disabled)
{
        struct debounce db;
        debounce_init(&db, 3u);
        debounce_disable(&db);

        debounce_reset(&db);

        TEST_ASSERT(db.enabled == false); /* NOT re-enabled */
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == false);
        TEST_ASSERT(db.trip == 3u);
}

/* Assert 3/5, deassert, then re-assert: requires full 5 ticks (no leftover) */
TEST_CASE(test_partial_count_deassert_reassert_requires_full_trip)
{
        struct debounce db;
        const uint16_t trip = 5u;
        debounce_init(&db, trip);

        /* Accumulate 3 of 5 */
        for (uint16_t i = 0u; i < 3u; i++) {
                debounce_update(&db, true);
        }
        TEST_ASSERT(db.counter == 3u);
        TEST_ASSERT(db.output == false);

        /* Deassert — counter resets */
        debounce_update(&db, false);
        TEST_ASSERT(db.counter == 0u);

        /* Re-assert: must take the full 5 ticks, not 2 */
        for (uint16_t i = 0u; i < (trip - 1u); i++) {
                bool result = debounce_update(&db, true);
                TEST_ASSERT(result == false);
        }

        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == true);
        TEST_ASSERT(db.counter == trip);
}

/* Query functions return clean defaults on a freshly-initialised object */
TEST_CASE(test_query_functions_on_fresh_object)
{
        struct debounce db;
        debounce_init(&db, 4u);

        TEST_ASSERT(debounce_is_active(&db) == false);
        TEST_ASSERT(debounce_is_latched(&db) == false);
        TEST_ASSERT(debounce_get_counter(&db) == 0u);
        TEST_ASSERT(debounce_get_trip(&db) == 4u);
        TEST_ASSERT(debounce_is_enabled(&db) == true);
}

/* Latch survives a disable/enable cycle */
TEST_CASE(test_latch_survives_disable_enable_cycle)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true);
        TEST_ASSERT(db.latch == true);
        TEST_ASSERT(db.output == true);

        debounce_disable(&db);
        TEST_ASSERT(db.latch == true);
        TEST_ASSERT(db.output == false);

        debounce_enable(&db);
        TEST_ASSERT(db.latch == true);   /* survived the cycle */
        TEST_ASSERT(db.output == false); /* clean starting state */
        TEST_ASSERT(db.counter == 0u);
}

/* trip=2: smallest non-trivial trip — 1 tick = no output, 2 ticks = output */
TEST_CASE(test_update_trip_two_boundary)
{
        struct debounce db;
        debounce_init(&db, 2u);

        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == false);
        TEST_ASSERT(db.counter == 1u);
        TEST_ASSERT(db.output == false);

        result = debounce_update(&db, true);
        TEST_ASSERT(result == true);
        TEST_ASSERT(db.counter == 2u);
        TEST_ASSERT(db.output == true);
}

/* ── auto-arm composition pattern ───────────────────────────────────────── */

/*
 * Demonstrate the two-debouncer auto-arm pattern described in the header.
 * A 'ready' debouncer gates the fault debouncer: the fault monitor is only
 * enabled once the system-ready condition has been debounced.
 */
TEST_CASE(test_composition_autoarm_pattern)
{
        struct debounce ready_db;
        struct debounce fault_db;

        const uint16_t ready_trip = 3u;
        const uint16_t fault_trip = 2u;

        debounce_init(&ready_db, ready_trip);
        debounce_init(&fault_db, fault_trip);
        debounce_disable(&fault_db); /* inhibit until armed */

        /* Tick 1-2: system_ready asserted but not yet debounced.
         * fault_db is disabled so fault input is ignored. */
        for (int i = 0; i < 2; i++) {
                bool armed = debounce_update(&ready_db, true);
                if (armed) {
                        debounce_enable(&fault_db);
                }
                /* fault_cond=true but monitor is still inhibited */
                bool fault = debounce_update(&fault_db, true);
                TEST_ASSERT(fault == false);
                TEST_ASSERT(debounce_is_enabled(&fault_db) == false);
        }

        /* Tick 3: system_ready debounced; fault_db is now armed. */
        {
                bool armed = debounce_update(&ready_db, true);
                TEST_ASSERT(armed == true);
                if (armed) {
                        debounce_enable(&fault_db);
                }
                TEST_ASSERT(debounce_is_enabled(&fault_db) == true);

                /* First fault tick after arming — not yet latched */
                bool fault = debounce_update(&fault_db, true);
                TEST_ASSERT(fault == false);
        }

        /* Tick 4: second consecutive fault tick — now latched */
        {
                debounce_update(&ready_db, true);
                bool fault = debounce_update(&fault_db, true);
                TEST_ASSERT(fault == true);
                TEST_ASSERT(debounce_is_latched(&fault_db) == true);
        }
}

/* ── debounce_set_trip (runtime trip reconfiguration) ───────────────────── */

/* Basic set_trip: changes trip, resets counter/output, preserves latch */
TEST_CASE(test_set_trip_basic)
{
        struct debounce db;
        debounce_init(&db, 3u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);

        bool ok = debounce_set_trip(&db, 10u);
        TEST_ASSERT(ok == true);
        TEST_ASSERT(db.trip == 10u);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == true);   /* preserved */
        TEST_ASSERT(db.enabled == true); /* preserved */
}

/* NULL db returns false */
TEST_CASE(test_set_trip_null_db)
{
        bool ok = debounce_set_trip(NULL, 5u);
        TEST_ASSERT(ok == false);
}

/* trip=0 returns false, struct unchanged */
TEST_CASE(test_set_trip_zero_returns_false)
{
        struct debounce db;
        debounce_init(&db, 5u);
        debounce_update(&db, true);

        bool ok = debounce_set_trip(&db, 0u);
        TEST_ASSERT(ok == false);
        TEST_ASSERT(db.trip == 5u);     /* unchanged */
        TEST_ASSERT(db.counter == 1u);  /* unchanged */
}

/* Lowering trip below current counter resets counter safely */
TEST_CASE(test_set_trip_lower_than_current_counter)
{
        struct debounce db;
        debounce_init(&db, 5u);

        for (int i = 0; i < 4; i++) {
                debounce_update(&db, true);
        }
        TEST_ASSERT(db.counter == 4u);

        debounce_set_trip(&db, 3u);
        TEST_ASSERT(db.counter == 0u); /* reset, not left dangling at 4 */
        TEST_ASSERT(db.trip == 3u);
}

/* set_trip while active clears output, preserves latch */
TEST_CASE(test_set_trip_while_active)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.latch == true);

        debounce_set_trip(&db, 5u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == true);
}

/* After set_trip, the new threshold governs assertion */
TEST_CASE(test_set_trip_then_count_to_new_trip)
{
        struct debounce db;
        debounce_init(&db, 10u);

        debounce_set_trip(&db, 3u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == false);

        bool result = debounce_update(&db, true);
        TEST_ASSERT(result == true);
        TEST_ASSERT(db.output == true);
}

/* set_trip while disabled preserves disabled state */
TEST_CASE(test_set_trip_while_disabled)
{
        struct debounce db;
        debounce_init(&db, 3u);
        debounce_disable(&db);

        bool ok = debounce_set_trip(&db, 7u);
        TEST_ASSERT(ok == true);
        TEST_ASSERT(db.trip == 7u);
        TEST_ASSERT(db.enabled == false); /* preserved */
}

/* ── symmetric (two-sided) debounce ─────────────────────────────────────── */

/* init_symmetric sets both thresholds correctly */
TEST_CASE(test_symmetric_init)
{
        struct debounce db;
        bool ok = debounce_init_symmetric(&db, 5u, 3u);

        TEST_ASSERT(ok == true);
        TEST_ASSERT(db.trip == 5u);
        TEST_ASSERT(db.fall_trip == 3u);
        TEST_ASSERT(db.counter == 0u);
        TEST_ASSERT(db.fall_counter == 0u);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == false);
        TEST_ASSERT(db.enabled == true);
        TEST_ASSERT(db.prev_output == false);
}

/* init_symmetric rejects NULL and rise_trip=0 */
TEST_CASE(test_symmetric_init_null_and_zero)
{
        struct debounce db;
        TEST_ASSERT(debounce_init_symmetric(NULL, 5u, 3u) == false);
        TEST_ASSERT(debounce_init_symmetric(&db, 0u, 3u) == false);

        /* fall_trip=0 is valid (means immediate de-assertion) */
        TEST_ASSERT(debounce_init_symmetric(&db, 5u, 0u) == true);
        TEST_ASSERT(db.fall_trip == 0u);
}

/* Basic symmetric operation: rise_trip=3, fall_trip=2 */
TEST_CASE(test_symmetric_basic)
{
        struct debounce db;
        debounce_init_symmetric(&db, 3u, 2u);

        /* Assert after 3 true ticks */
        TEST_ASSERT(debounce_update(&db, true) == false);
        TEST_ASSERT(debounce_update(&db, true) == false);
        TEST_ASSERT(debounce_update(&db, true) == true);
        TEST_ASSERT(db.output == true);

        /* Output stays true during fall debounce */
        TEST_ASSERT(debounce_update(&db, false) == true); /* 1 of 2 */
        TEST_ASSERT(db.output == true);

        /* Output clears on 2nd false tick */
        TEST_ASSERT(debounce_update(&db, false) == false);
        TEST_ASSERT(db.output == false);
}

/* fall_trip=0 behaves identically to one-sided debounce */
TEST_CASE(test_symmetric_fall_trip_zero_is_immediate)
{
        struct debounce db;
        debounce_init_symmetric(&db, 2u, 0u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);

        debounce_update(&db, false);
        TEST_ASSERT(db.output == false); /* immediate */
}

/* Partial fall count resets when input re-asserts */
TEST_CASE(test_symmetric_partial_fall_resets_on_reassert)
{
        struct debounce db;
        debounce_init_symmetric(&db, 2u, 3u);

        /* Assert output */
        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);

        /* 2 of 3 false ticks — output still held */
        debounce_update(&db, false);
        debounce_update(&db, false);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.fall_counter == 2u);

        /* Re-assert: fall_counter resets, output stays true */
        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);
        TEST_ASSERT(db.fall_counter == 0u);
}

/* Latch persists through symmetric fall debounce */
TEST_CASE(test_symmetric_latch_preserved)
{
        struct debounce db;
        debounce_init_symmetric(&db, 1u, 2u);

        debounce_update(&db, true);
        TEST_ASSERT(db.latch == true);

        debounce_update(&db, false);
        debounce_update(&db, false);
        TEST_ASSERT(db.output == false);
        TEST_ASSERT(db.latch == true); /* sticky */
}

/* disable clears fall_counter */
TEST_CASE(test_symmetric_disable_clears_fall_counter)
{
        struct debounce db;
        debounce_init_symmetric(&db, 2u, 3u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        debounce_update(&db, false);
        TEST_ASSERT(db.fall_counter == 1u);

        debounce_disable(&db);
        TEST_ASSERT(db.fall_counter == 0u);
}

/* reset clears fall_counter */
TEST_CASE(test_symmetric_reset_clears_fall_counter)
{
        struct debounce db;
        debounce_init_symmetric(&db, 2u, 3u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        debounce_update(&db, false);
        TEST_ASSERT(db.fall_counter == 1u);

        debounce_reset(&db);
        TEST_ASSERT(db.fall_counter == 0u);
        TEST_ASSERT(db.fall_trip == 3u); /* preserved */
}

/* Runtime set_fall_trip works and resets fall_counter */
TEST_CASE(test_symmetric_set_fall_trip_runtime)
{
        struct debounce db;
        debounce_init_symmetric(&db, 2u, 5u);

        debounce_update(&db, true);
        debounce_update(&db, true);
        debounce_update(&db, false);
        TEST_ASSERT(db.fall_counter == 1u);

        bool ok = debounce_set_fall_trip(&db, 2u);
        TEST_ASSERT(ok == true);
        TEST_ASSERT(db.fall_trip == 2u);
        TEST_ASSERT(db.fall_counter == 0u);
}

/* set_fall_trip NULL returns false */
TEST_CASE(test_set_fall_trip_null_db)
{
        TEST_ASSERT(debounce_set_fall_trip(NULL, 5u) == false);
}

/* get_fall_trip returns the fall threshold */
TEST_CASE(test_get_fall_trip_returns_value)
{
        struct debounce db;
        debounce_init_symmetric(&db, 2u, 7u);

        TEST_ASSERT(debounce_get_fall_trip(&db) == 7u);
        TEST_ASSERT(debounce_get_fall_trip(NULL) == 0u);
}

/* When output is already false, false ticks do not advance fall_counter */
TEST_CASE(test_symmetric_output_not_asserted_fall_noop)
{
        struct debounce db;
        debounce_init_symmetric(&db, 3u, 2u);

        /* Output is false, feed false ticks */
        debounce_update(&db, false);
        debounce_update(&db, false);
        TEST_ASSERT(db.fall_counter == 0u);
        TEST_ASSERT(db.output == false);
}

/* legacy debounce_init sets fall_trip=0 and fall_counter=0 */
TEST_CASE(test_legacy_init_sets_fall_trip_zero)
{
        struct debounce db;
        debounce_init(&db, 5u);

        TEST_ASSERT(db.fall_trip == 0u);
        TEST_ASSERT(db.fall_counter == 0u);
}

/* Edge case: trip=1, fall_trip=1 */
TEST_CASE(test_symmetric_trip_one_fall_one)
{
        struct debounce db;
        debounce_init_symmetric(&db, 1u, 1u);

        TEST_ASSERT(debounce_update(&db, true) == true);
        TEST_ASSERT(db.output == true);

        TEST_ASSERT(debounce_update(&db, false) == false);
        TEST_ASSERT(db.output == false);
}

/* ── edge detection (debounce_rose / debounce_fell) ─────────────────────── */

/* rose returns true for exactly one tick on assertion */
TEST_CASE(test_rose_on_assertion)
{
        struct debounce db;
        debounce_init(&db, 2u);

        debounce_update(&db, true);
        TEST_ASSERT(debounce_rose(&db) == false); /* not yet tripped */

        debounce_update(&db, true);
        TEST_ASSERT(debounce_rose(&db) == true); /* just asserted */

        debounce_update(&db, true);
        TEST_ASSERT(debounce_rose(&db) == false); /* steady state */
}

/* fell returns true for exactly one tick on de-assertion */
TEST_CASE(test_fell_on_deassertion)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);

        debounce_update(&db, false);
        TEST_ASSERT(debounce_fell(&db) == true); /* just de-asserted */

        debounce_update(&db, false);
        TEST_ASSERT(debounce_fell(&db) == false); /* steady false */
}

/* NULL returns false for both */
TEST_CASE(test_rose_fell_null_db)
{
        TEST_ASSERT(debounce_rose(NULL) == false);
        TEST_ASSERT(debounce_fell(NULL) == false);
}

/* rose is false while counter is accumulating */
TEST_CASE(test_rose_not_during_counting)
{
        struct debounce db;
        debounce_init(&db, 5u);

        for (int i = 0; i < 4; i++) {
                debounce_update(&db, true);
                TEST_ASSERT(debounce_rose(&db) == false);
        }
}

/* With symmetric debounce, fell fires when output actually clears */
TEST_CASE(test_fell_with_symmetric_debounce)
{
        struct debounce db;
        debounce_init_symmetric(&db, 1u, 3u);

        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);

        /* 1st and 2nd false ticks: output still held */
        debounce_update(&db, false);
        TEST_ASSERT(debounce_fell(&db) == false);
        debounce_update(&db, false);
        TEST_ASSERT(debounce_fell(&db) == false);

        /* 3rd false tick: output transitions */
        debounce_update(&db, false);
        TEST_ASSERT(debounce_fell(&db) == true);
        TEST_ASSERT(db.output == false);
}

/* No spurious edges after reset */
TEST_CASE(test_rose_fell_after_reset)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true);
        debounce_reset(&db);

        TEST_ASSERT(debounce_rose(&db) == false);
        TEST_ASSERT(debounce_fell(&db) == false);
}

/* No spurious edges after disable/enable cycle */
TEST_CASE(test_rose_fell_after_disable_enable)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true);
        TEST_ASSERT(db.output == true);

        debounce_disable(&db);
        debounce_enable(&db);

        TEST_ASSERT(debounce_rose(&db) == false);
        TEST_ASSERT(debounce_fell(&db) == false);
}

/* rose is true for exactly one tick, then false even if output stays true */
TEST_CASE(test_rose_single_tick_only)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true);
        TEST_ASSERT(debounce_rose(&db) == true);

        /* Next update with same input: rose must be false */
        debounce_update(&db, true);
        TEST_ASSERT(debounce_rose(&db) == false);

        debounce_update(&db, true);
        TEST_ASSERT(debounce_rose(&db) == false);
}

/* fell is true for exactly one tick */
TEST_CASE(test_fell_single_tick_only)
{
        struct debounce db;
        debounce_init(&db, 1u);

        debounce_update(&db, true);
        debounce_update(&db, false);
        TEST_ASSERT(debounce_fell(&db) == true);

        debounce_update(&db, false);
        TEST_ASSERT(debounce_fell(&db) == false);
}

/* Both rose and fell return false while disabled */
TEST_CASE(test_no_edges_while_disabled)
{
        struct debounce db;
        debounce_init(&db, 1u);
        debounce_disable(&db);

        debounce_update(&db, true);
        TEST_ASSERT(debounce_rose(&db) == false);
        TEST_ASSERT(debounce_fell(&db) == false);
}

/* ── runner ──────────────────────────────────────────────────────────────── */

static void
run_test(void (*test_func)(void), const char *name)
{
        test_func();
        TEST_PASS(name);
}

int
main(void)
{
        fprintf(stdout, "\n=== Running debounce unit tests ===\n\n");

        run_test(test_init_sets_trip_and_defaults,
                 "test_init_sets_trip_and_defaults");
        run_test(test_init_null_db, "test_init_null_db");
        run_test(test_init_zero_trip_returns_false,
                 "test_init_zero_trip_returns_false");

        run_test(test_update_counter_increments_before_trip,
                 "test_update_counter_increments_before_trip");
        run_test(test_update_output_asserts_at_trip,
                 "test_update_output_asserts_at_trip");
        run_test(test_update_deassert_clears_output_preserves_latch,
                 "test_update_deassert_clears_output_preserves_latch");
        run_test(test_update_trip_one_asserts_on_first_tick,
                 "test_update_trip_one_asserts_on_first_tick");
        run_test(test_update_counter_saturates_at_trip,
                 "test_update_counter_saturates_at_trip");
        run_test(test_update_counter_saturates_at_uint16_max_trip,
                 "test_update_counter_saturates_at_uint16_max_trip");
        run_test(test_update_reasserts_after_deassert,
                 "test_update_reasserts_after_deassert");
        run_test(test_update_partial_count_then_deassert_resets_counter,
                 "test_update_partial_count_then_deassert_resets_counter");
        run_test(test_update_null_db_returns_false,
                 "test_update_null_db_returns_false");
        run_test(test_update_zero_trip_returns_false,
                 "test_update_zero_trip_returns_false");
        run_test(test_update_noop_when_disabled,
                 "test_update_noop_when_disabled");

        run_test(test_latch_set_on_output_and_persists_after_deassert,
                 "test_latch_set_on_output_and_persists_after_deassert");
        run_test(test_clear_latch_clears_only_latch,
                 "test_clear_latch_clears_only_latch");
        run_test(test_latch_resets_after_clear_then_reasserts,
                 "test_latch_resets_after_clear_then_reasserts");
        run_test(test_clear_latch_null_db, "test_clear_latch_null_db");

        run_test(test_reset_clears_debounce_state_preserves_config,
                 "test_reset_clears_debounce_state_preserves_config");
        run_test(test_reset_null_db, "test_reset_null_db");

        run_test(test_enable_sets_enabled_only,
                 "test_enable_sets_enabled_only");
        run_test(
            test_disable_clears_gate_and_transient_state_preserves_latch,
            "test_disable_clears_gate_and_transient_state_preserves_latch");
        run_test(test_is_enabled_reflects_state,
                 "test_is_enabled_reflects_state");
        run_test(test_disable_then_enable_gives_clean_state,
                 "test_disable_then_enable_gives_clean_state");

        run_test(test_is_active_reflects_output,
                 "test_is_active_reflects_output");
        run_test(test_get_counter_returns_counter,
                 "test_get_counter_returns_counter");
        run_test(test_get_trip_returns_trip, "test_get_trip_returns_trip");

        run_test(test_update_rapid_toggling_never_asserts,
                 "test_update_rapid_toggling_never_asserts");
        run_test(test_reinit_on_live_object,
                 "test_reinit_on_live_object");
        run_test(test_double_disable_is_idempotent,
                 "test_double_disable_is_idempotent");
        run_test(test_double_enable_is_idempotent,
                 "test_double_enable_is_idempotent");
        run_test(test_clear_latch_when_already_clear_is_noop,
                 "test_clear_latch_when_already_clear_is_noop");
        run_test(test_reset_while_disabled_preserves_disabled,
                 "test_reset_while_disabled_preserves_disabled");
        run_test(test_partial_count_deassert_reassert_requires_full_trip,
                 "test_partial_count_deassert_reassert_requires_full_trip");
        run_test(test_query_functions_on_fresh_object,
                 "test_query_functions_on_fresh_object");
        run_test(test_latch_survives_disable_enable_cycle,
                 "test_latch_survives_disable_enable_cycle");
        run_test(test_update_trip_two_boundary,
                 "test_update_trip_two_boundary");

        run_test(test_composition_autoarm_pattern,
                 "test_composition_autoarm_pattern");

        /* set_trip */
        run_test(test_set_trip_basic, "test_set_trip_basic");
        run_test(test_set_trip_null_db, "test_set_trip_null_db");
        run_test(test_set_trip_zero_returns_false,
                 "test_set_trip_zero_returns_false");
        run_test(test_set_trip_lower_than_current_counter,
                 "test_set_trip_lower_than_current_counter");
        run_test(test_set_trip_while_active,
                 "test_set_trip_while_active");
        run_test(test_set_trip_then_count_to_new_trip,
                 "test_set_trip_then_count_to_new_trip");
        run_test(test_set_trip_while_disabled,
                 "test_set_trip_while_disabled");

        /* symmetric debounce */
        run_test(test_symmetric_init, "test_symmetric_init");
        run_test(test_symmetric_init_null_and_zero,
                 "test_symmetric_init_null_and_zero");
        run_test(test_symmetric_basic, "test_symmetric_basic");
        run_test(test_symmetric_fall_trip_zero_is_immediate,
                 "test_symmetric_fall_trip_zero_is_immediate");
        run_test(test_symmetric_partial_fall_resets_on_reassert,
                 "test_symmetric_partial_fall_resets_on_reassert");
        run_test(test_symmetric_latch_preserved,
                 "test_symmetric_latch_preserved");
        run_test(test_symmetric_disable_clears_fall_counter,
                 "test_symmetric_disable_clears_fall_counter");
        run_test(test_symmetric_reset_clears_fall_counter,
                 "test_symmetric_reset_clears_fall_counter");
        run_test(test_symmetric_set_fall_trip_runtime,
                 "test_symmetric_set_fall_trip_runtime");
        run_test(test_set_fall_trip_null_db,
                 "test_set_fall_trip_null_db");
        run_test(test_get_fall_trip_returns_value,
                 "test_get_fall_trip_returns_value");
        run_test(test_symmetric_output_not_asserted_fall_noop,
                 "test_symmetric_output_not_asserted_fall_noop");
        run_test(test_legacy_init_sets_fall_trip_zero,
                 "test_legacy_init_sets_fall_trip_zero");
        run_test(test_symmetric_trip_one_fall_one,
                 "test_symmetric_trip_one_fall_one");

        /* edge detection */
        run_test(test_rose_on_assertion, "test_rose_on_assertion");
        run_test(test_fell_on_deassertion, "test_fell_on_deassertion");
        run_test(test_rose_fell_null_db, "test_rose_fell_null_db");
        run_test(test_rose_not_during_counting,
                 "test_rose_not_during_counting");
        run_test(test_fell_with_symmetric_debounce,
                 "test_fell_with_symmetric_debounce");
        run_test(test_rose_fell_after_reset,
                 "test_rose_fell_after_reset");
        run_test(test_rose_fell_after_disable_enable,
                 "test_rose_fell_after_disable_enable");
        run_test(test_rose_single_tick_only,
                 "test_rose_single_tick_only");
        run_test(test_fell_single_tick_only,
                 "test_fell_single_tick_only");
        run_test(test_no_edges_while_disabled,
                 "test_no_edges_while_disabled");

        fprintf(stdout, "\n=== All tests passed ===\n\n");
        return EXIT_SUCCESS;
}
