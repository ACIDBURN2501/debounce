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

        run_test(test_composition_autoarm_pattern,
                 "test_composition_autoarm_pattern");

        fprintf(stdout, "\n=== All tests passed ===\n\n");
        return EXIT_SUCCESS;
}
