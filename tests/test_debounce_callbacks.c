/*
 * @file test_debounce_callbacks.c
 * @brief Unit tests for the optional transition-callback mechanism.
 *
 * Compiled with -DDEBOUNCE_ENABLE_CALLBACKS=1.
 */

#define DEBOUNCE_ENABLE_CALLBACKS 1

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

/* ── shared callback state ──────────────────────────────────────────────── */

static int g_rise_count;
static int g_fall_count;
static struct debounce *g_last_db;

static void
reset_cb_state(void)
{
        g_rise_count = 0;
        g_fall_count = 0;
        g_last_db    = NULL;
}

static void
test_callback(struct debounce *db, bool rose)
{
        g_last_db = db;
        if (rose) {
                g_rise_count++;
        } else {
                g_fall_count++;
        }
}

/* ── tests ──────────────────────────────────────────────────────────────── */

/* Callback fires on rising edge */
TEST_CASE(test_callback_fires_on_rising_edge)
{
        struct debounce db;
        debounce_init(&db, 2u);
        debounce_set_callback(&db, test_callback);
        reset_cb_state();

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 0); /* not yet tripped */

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1);
        TEST_ASSERT(g_last_db == &db);
}

/* Callback fires on falling edge */
TEST_CASE(test_callback_fires_on_falling_edge)
{
        struct debounce db;
        debounce_init(&db, 1u);
        debounce_set_callback(&db, test_callback);
        reset_cb_state();

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1);

        debounce_update(&db, false);
        TEST_ASSERT(g_fall_count == 1);
        TEST_ASSERT(g_last_db == &db);
}

/* Callback not invoked during steady state */
TEST_CASE(test_callback_not_fired_while_steady)
{
        struct debounce db;
        debounce_init(&db, 1u);
        debounce_set_callback(&db, test_callback);
        reset_cb_state();

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1);

        /* Steady true: no more callbacks */
        debounce_update(&db, true);
        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1);

        debounce_update(&db, false);
        TEST_ASSERT(g_fall_count == 1);

        /* Steady false: no more callbacks */
        debounce_update(&db, false);
        debounce_update(&db, false);
        TEST_ASSERT(g_fall_count == 1);
}

/* NULL callback is safe — no crash */
TEST_CASE(test_callback_null_is_safe)
{
        struct debounce db;
        debounce_init(&db, 1u);
        /* callback defaults to NULL after init */

        debounce_update(&db, true);
        debounce_update(&db, false);
        /* No crash — test passes by not crashing */
}

/* Set callback then clear it */
TEST_CASE(test_callback_set_and_clear)
{
        struct debounce db;
        debounce_init(&db, 1u);
        debounce_set_callback(&db, test_callback);
        reset_cb_state();

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1);

        debounce_update(&db, false);
        debounce_set_callback(&db, NULL);

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1); /* no further callbacks */
}

/* Callback survives reset */
TEST_CASE(test_callback_survives_reset)
{
        struct debounce db;
        debounce_init(&db, 1u);
        debounce_set_callback(&db, test_callback);
        reset_cb_state();

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1);

        debounce_reset(&db);
        TEST_ASSERT(db.callback == test_callback); /* preserved */

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 2);
}

/* Falling edge callback fires after symmetric fall debounce */
TEST_CASE(test_callback_with_symmetric_debounce)
{
        struct debounce db;
        debounce_init_symmetric(&db, 1u, 3u);
        debounce_set_callback(&db, test_callback);
        reset_cb_state();

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 1);

        /* 2 of 3 false ticks: no falling callback yet */
        debounce_update(&db, false);
        debounce_update(&db, false);
        TEST_ASSERT(g_fall_count == 0);

        /* 3rd false tick: now falls */
        debounce_update(&db, false);
        TEST_ASSERT(g_fall_count == 1);
}

/* No callback while disabled */
TEST_CASE(test_callback_not_fired_when_disabled)
{
        struct debounce db;
        debounce_init(&db, 1u);
        debounce_set_callback(&db, test_callback);
        reset_cb_state();

        debounce_disable(&db);

        debounce_update(&db, true);
        TEST_ASSERT(g_rise_count == 0);
        TEST_ASSERT(g_fall_count == 0);
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
        fprintf(stdout, "\n=== Running debounce callback tests ===\n\n");

        run_test(test_callback_fires_on_rising_edge,
                 "test_callback_fires_on_rising_edge");
        run_test(test_callback_fires_on_falling_edge,
                 "test_callback_fires_on_falling_edge");
        run_test(test_callback_not_fired_while_steady,
                 "test_callback_not_fired_while_steady");
        run_test(test_callback_null_is_safe,
                 "test_callback_null_is_safe");
        run_test(test_callback_set_and_clear,
                 "test_callback_set_and_clear");
        run_test(test_callback_survives_reset,
                 "test_callback_survives_reset");
        run_test(test_callback_with_symmetric_debounce,
                 "test_callback_with_symmetric_debounce");
        run_test(test_callback_not_fired_when_disabled,
                 "test_callback_not_fired_when_disabled");

        fprintf(stdout, "\n=== All callback tests passed ===\n\n");
        return EXIT_SUCCESS;
}
