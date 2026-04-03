/*
 * @file debounce.h
 * @brief Public API for a generic saturating-counter debounce primitive.
 *
 * @details
 *    Provides a deterministic, allocation-free debounce primitive suitable
 *    for use in ISRs and periodic control loops.  All state is held in a
 *    caller-owned `struct debounce`; no global state is used.
 *
 *    The module is implemented entirely as `static inline` functions so that
 *    it incurs zero function-call overhead after optimisation, and so that no
 *    separate link step is required by consumers.
 *
 *    MISRA C 2012 awareness
 *    ----------------------
 *    The implementation targets MISRA C 2012.  The following advisory
 *    deviations are documented here:
 *
 *      Rule 15.5 (advisory):  Each function has a single point of exit
 *        (the final `return` statement).  Guard clauses at the top of each
 *        function use a local `result` variable to carry the default value
 *        rather than issuing an early return.
 *
 *    IEC 61508 awareness
 *    -------------------
 *    Defensive NULL checks are applied at every API boundary.  All state
 *    is deterministic: no dynamic memory, no recursion, no data-dependent
 *    loop bounds.  `trip == 0` is an invalid configuration and is rejected
 *    by debounce_init() at the point of object creation.
 *
 *
 *    Basic usage
 *    -----------
 *        #include "debounce.h"
 *
 *        static struct debounce db;
 *
 *        void system_init(void) {
 *            if (!debounce_init(&db, TRIP_TICKS)) {
 *                // handle invalid configuration
 *            }
 *        }
 *
 *        void control_loop_tick(void) {
 *            bool active  = debounce_update(&db, condition_met);
 *            bool latched = debounce_is_latched(&db);
 *
 *            if (fault_acknowledged) {
 *                debounce_clear_latch(&db);
 *            }
 *        }
 *
 *
 *    Symmetric (two-sided) debounce
 *    ------------------------------
 *    By default, assertion is debounced but de-assertion is immediate.
 *    Use debounce_init_symmetric() to debounce both edges:
 *
 *        static struct debounce sw;
 *
 *        void init(void) {
 *            // 5 ticks to assert, 3 ticks to de-assert
 *            debounce_init_symmetric(&sw, 5u, 3u);
 *        }
 *
 *        void tick(bool pressed) {
 *            debounce_update(&sw, pressed);
 *            if (debounce_rose(&sw))  { on_press();  }
 *            if (debounce_fell(&sw))  { on_release(); }
 *        }
 *
 *
 *    Auto-arm / gated-monitoring pattern
 *    ------------------------------------
 *    Use two independent `debounce` objects: one debounces the "system ready"
 *    condition, and its output gates the fault monitor.  This is cleaner and
 *    more auditable than a combined auto-arm function.
 *
 *        static struct debounce ready_db;
 *        static struct debounce fault_db;
 *
 *        void system_init(void) {
 *            (void)debounce_init(&ready_db, READY_TRIP);
 *            (void)debounce_init(&fault_db, FAULT_TRIP);
 *            debounce_disable(&fault_db);   // inhibit until system is ready
 *        }
 *
 *        void tick(bool system_ready, bool fault_cond) {
 *            if (debounce_update(&ready_db, system_ready)) {
 *                debounce_enable(&fault_db); // arm once ready is debounced
 *            }
 *            bool fault = debounce_update(&fault_db, fault_cond);
 *        }
 */

#ifndef DEBOUNCE_H_
#define DEBOUNCE_H_

#ifdef __cplusplus
extern "C" {
#endif

/* ================ INCLUDES ================================================ */

#include "debounce_conf.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ================ FORWARD DECLARATIONS ==================================== */

struct debounce;

/* ================ CALLBACK TYPEDEF ======================================== */

#if DEBOUNCE_ENABLE_CALLBACKS
/**
 * @typedef debounce_callback_t
 * @brief   Optional transition callback signature.
 *
 * @param db    Pointer to the debounce object whose output transitioned.
 * @param rose  true on a rising edge (output went false -> true);
 *              false on a falling edge (output went true -> false).
 *
 * @note  Invoked from within debounce_update().  Keep callbacks short and
 *        non-blocking, especially in ISR context.
 */
typedef void (*debounce_callback_t)(struct debounce *db, bool rose);
#endif

/* ================ STRUCTURES ============================================== */

/**
 * @struct debounce
 * @brief  Saturating-counter debouncer state.
 *
 * Initialise with debounce_init() or debounce_init_symmetric() before first
 * use.  Do not access fields directly; use the provided API functions to
 * preserve invariants.
 *
 * @par Struct invariants (hold after a successful debounce_init() call)
 *   - `counter` is always in the range [0, trip].
 *   - `fall_counter` is always in the range [0, fall_trip].
 *   - `latch` is only true if `output` has been true at least once since
 *     the last debounce_clear_latch() or debounce_reset() call.
 *   - `trip` is never zero after a successful debounce_init() call.
 *
 * @var trip          Configured assertion threshold (ticks).  Set by
 *                    debounce_init() or debounce_set_trip().
 *                    A value of 0 is invalid; debounce_init() rejects it.
 *
 * @var counter       Consecutive asserted ticks in the range [0, trip].
 *                    Saturates at `trip`; never exceeds it.
 *
 * @var fall_trip     De-assertion threshold (ticks).  When 0 (the default),
 *                    de-assertion is immediate (one-sided debounce).  Set by
 *                    debounce_init_symmetric() or debounce_set_fall_trip().
 *
 * @var fall_counter  Consecutive de-asserted ticks in [0, fall_trip].
 *                    Saturates at `fall_trip`; never exceeds it.
 *
 * @var output        Current debounced output.  Becomes true once `counter`
 *                    reaches `trip`; resets to false when the input clears
 *                    (or after `fall_trip` de-asserted ticks in symmetric
 *                    mode).  Read via debounce_is_active().
 *
 * @var latch         Sticky fault latch.  Set whenever `output` becomes true;
 *                    NOT cleared when the input clears.  Must be cleared
 *                    explicitly with debounce_clear_latch().
 *                    Read via debounce_is_latched().
 *
 * @var enabled       Processing gate.  When false, debounce_update() is a
 *                    no-op and returns false.  Controlled via
 *                    debounce_enable() and debounce_disable().
 *
 * @var prev_output   Previous tick's debounced output, for edge detection.
 *                    Read via debounce_rose() and debounce_fell().
 *
 * @var callback      (Optional, compile-time) Transition callback; invoked
 *                    from debounce_update() on rising and falling edges.
 *                    Requires DEBOUNCE_ENABLE_CALLBACKS=1.
 */
struct debounce {
        uint16_t trip;
        uint16_t counter;
        uint16_t fall_trip;
        uint16_t fall_counter;
        bool     output;
        bool     latch;
        bool     enabled;
        bool     prev_output;
#if DEBOUNCE_ENABLE_CALLBACKS
        debounce_callback_t callback;
#endif
};

/* ================ GLOBAL PROTOTYPES ======================================= */

/**
 * @brief Initialise a debounce object.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 * @param trip  Number of consecutive asserted ticks required to assert the
 *              output.  Must be greater than zero.
 *
 * @return  true  if the object was successfully initialised.
 * @return  false if @p db is NULL or @p trip is zero; the object is not
 *               modified in either case.
 *
 * @pre  @p db is not NULL.
 * @pre  @p trip is greater than zero.
 *
 * @post On success: `db->trip == trip`, `db->counter == 0`,
 *       `db->fall_trip == 0`, `db->fall_counter == 0`,
 *       `db->output == false`, `db->latch == false`,
 *       `db->prev_output == false`, `db->enabled == true`.
 * @post On failure: the object pointed to by @p db is not modified.
 *
 * @note  Call this function once before any other API function on the
 *        object.  If a disabled-at-start behaviour is required, follow
 *        with debounce_disable().
 *
 * @note  MISRA C 2012 Rule 17.7 — the return value shall be used by the
 *        caller.  On safety-critical targets, a false return should be
 *        treated as a configuration fault.
 */
static inline bool
debounce_init(struct debounce *db, uint16_t trip)
{
        bool result = false;

        if ((db != NULL) && (trip != 0u)) {
                db->trip         = trip;
                db->counter      = 0u;
                db->fall_trip    = 0u;
                db->fall_counter = 0u;
                db->output       = false;
                db->latch        = false;
                db->enabled      = true;
                db->prev_output  = false;
#if DEBOUNCE_ENABLE_CALLBACKS
                db->callback = NULL;
#endif
                result = true;
        }

        return result;
}

/**
 * @brief Reset debounce state to the initial (unasserted) condition.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post `db->counter == 0`, `db->fall_counter == 0`,
 *       `db->output == false`, `db->latch == false`,
 *       `db->prev_output == false`.
 * @post `db->trip`, `db->fall_trip`, and `db->enabled` are unchanged.
 *
 * @note  To clear only the sticky latch without resetting the counter or
 *        output, use debounce_clear_latch() instead.
 */
static inline void
debounce_reset(struct debounce *db)
{
        if (db != NULL) {
                db->counter      = 0u;
                db->fall_counter = 0u;
                db->output       = false;
                db->latch        = false;
                db->prev_output  = false;
        }
}

/**
 * @brief Process one tick of the monitored condition.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 * @param cond  True if the monitored condition is asserted this tick.
 *
 * @return  The current debounced output:
 *            - true  once `counter` has reached `trip` consecutive
 *                    asserted ticks.
 *            - false while still counting, when `cond` clears, when
 *                    the object is not enabled, or when the guard
 *                    conditions below are not met.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init()
 *       (guarantees `db->trip != 0`).
 *
 * @post If @p cond is true and `db->counter` reaches `db->trip`:
 *       `db->output == true` and `db->latch == true`.
 * @post If @p cond is false: `db->counter == 0`.  When `db->fall_trip`
 *       is zero, `db->output` is cleared immediately.  When
 *       `db->fall_trip` is non-zero, `db->output` remains true until
 *       `db->fall_counter` reaches `db->fall_trip`.
 * @post `db->latch` is never cleared by this function.
 * @post `db->trip`, `db->fall_trip`, and `db->enabled` are never modified
 *       by this function.
 *
 * @note  When `fall_trip` is zero (the default after debounce_init()),
 *        `counter` and `output` are reset immediately when `cond` clears
 *        (one-sided debounce).  When `fall_trip` is non-zero, the output
 *        is held until `fall_trip` consecutive de-asserted ticks are seen
 *        (two-sided debounce).
 *        The sticky `latch` is NOT cleared; use debounce_clear_latch().
 *
 * @note  When not enabled, this function is a complete no-op: no fields
 *        are modified and false is returned.  Call debounce_disable() to
 *        put the object into a known clean state before disabling.
 *
 * @note  MISRA C 2012 Rule 13.4 — The result of the counter increment is
 *        not used directly in a comparison; the increment and comparison
 *        are separate statements.
 */
static inline bool
debounce_update(struct debounce *db, bool cond)
{
        bool result = false;

        if ((db != NULL) && (db->trip != 0u) && db->enabled) {
                db->prev_output = db->output;

                if (cond) {
                        db->fall_counter = 0u;
                        if (db->counter < db->trip) {
                                db->counter++;
                        }
                        if (db->counter >= db->trip) {
                                db->output = true;
                                db->latch  = true;
                        }
                } else {
                        db->counter = 0u;
                        if (db->fall_trip == 0u) {
                                db->output       = false;
                                db->fall_counter = 0u;
                        } else if (db->output) {
                                if (db->fall_counter < db->fall_trip) {
                                        db->fall_counter++;
                                }
                                if (db->fall_counter >= db->fall_trip) {
                                        db->output       = false;
                                        db->fall_counter = 0u;
                                }
                        } else {
                                db->fall_counter = 0u;
                        }
                }

#if DEBOUNCE_ENABLE_CALLBACKS
                if (db->callback != NULL) {
                        if (db->output && !db->prev_output) {
                                db->callback(db, true);
                        } else if (!db->output && db->prev_output) {
                                db->callback(db, false);
                        }
                }
#endif
                result = db->output;
        }

        return result;
}

/**
 * @brief Query the current debounced output.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @return  True if the debounced output is currently asserted; false
 *          otherwise or if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post The object is not modified.
 *
 * @note  This is a non-destructive read; it does not process a tick.
 *        Use debounce_update() to process a tick and obtain the result
 *        in a single call.
 */
static inline bool
debounce_is_active(const struct debounce *db)
{
        bool result = false;

        if (db != NULL) {
                result = db->output;
        }

        return result;
}

/**
 * @brief Query the sticky latch.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @return  True if the latch has been set (i.e. `output` has been true at
 *          least once since the last debounce_clear_latch() or
 *          debounce_reset() call); false otherwise or if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post The object is not modified.
 *
 * @note  The latch is set the moment `output` first goes true and is NOT
 *        cleared when the input condition or the output clears.  It must
 *        be cleared explicitly with debounce_clear_latch() or
 *        debounce_reset().  This models a fault-hold behaviour: the fault
 *        record persists until an operator or supervisor acknowledges it.
 */
static inline bool
debounce_is_latched(const struct debounce *db)
{
        bool result = false;

        if (db != NULL) {
                result = db->latch;
        }

        return result;
}

/**
 * @brief Clear the sticky latch.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post `db->latch == false`.
 * @post `db->counter`, `db->output`, and `db->enabled` are unchanged.
 *
 * @note  If the debounced output is currently asserted (`output == true`),
 *        the latch will be re-set on the next call to debounce_update().
 *        To prevent immediate re-latch, ensure the monitored condition has
 *        cleared before calling this function.
 */
static inline void
debounce_clear_latch(struct debounce *db)
{
        if (db != NULL) {
                db->latch = false;
        }
}

/**
 * @brief Query the current saturating counter value.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @return  The current consecutive-assertion tick count in [0, trip];
 *          0 if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post The object is not modified.
 */
static inline uint16_t
debounce_get_counter(const struct debounce *db)
{
        uint16_t result = 0u;

        if (db != NULL) {
                result = db->counter;
        }

        return result;
}

/**
 * @brief Query the configured trip threshold.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @return  The trip threshold set by debounce_init(); 0 if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post The object is not modified.
 */
static inline uint16_t
debounce_get_trip(const struct debounce *db)
{
        uint16_t result = 0u;

        if (db != NULL) {
                result = db->trip;
        }

        return result;
}

/**
 * @brief Enable processing.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post `db->enabled == true`.
 * @post No other fields are modified.
 *
 * @note  debounce_disable() already clears `counter`, `fall_counter`,
 *        `output`, and `prev_output`, so re-enabling after a disable
 *        gives a clean starting state without requiring a separate
 *        debounce_reset() call.
 */
static inline void
debounce_enable(struct debounce *db)
{
        if (db != NULL) {
                db->enabled = true;
        }
}

/**
 * @brief Disable processing and clear transient state.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post `db->enabled == false`, `db->counter == 0`,
 *       `db->fall_counter == 0`, `db->output == false`,
 *       `db->prev_output == false`.
 * @post `db->latch` is unchanged (intentionally preserved; see note).
 * @post `db->trip` and `db->fall_trip` are unchanged.
 *
 * @note  The sticky `latch` is intentionally preserved so that a fault
 *        record is not silently discarded by a disable/enable cycle.  Use
 *        debounce_clear_latch() or debounce_reset() to clear it explicitly.
 *
 * @note  After this call, debounce_update() is a no-op returning false and
 *        debounce_is_active() returns false until debounce_enable() is
 *        called.
 */
static inline void
debounce_disable(struct debounce *db)
{
        if (db != NULL) {
                db->enabled      = false;
                db->counter      = 0u;
                db->fall_counter = 0u;
                db->output       = false;
                db->prev_output  = false;
        }
}

/**
 * @brief Query whether the debouncer is enabled.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 *
 * @return  True if enabled; false if disabled or if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init().
 *
 * @post The object is not modified.
 */
static inline bool
debounce_is_enabled(const struct debounce *db)
{
        bool result = false;

        if (db != NULL) {
                result = db->enabled;
        }

        return result;
}

/* ── symmetric (two-sided) debounce ──────────────────────────────────────── */

/**
 * @brief Initialise a debounce object with separate rise and fall thresholds.
 *
 * @param db         Pointer to the debounce object (must be non-NULL).
 * @param rise_trip  Consecutive asserted ticks to assert the output.
 *                   Must be greater than zero.
 * @param fall_trip  Consecutive de-asserted ticks to clear the output.
 *                   Zero means immediate de-assertion (one-sided debounce).
 *
 * @return  true  if the object was successfully initialised.
 * @return  false if @p db is NULL or @p rise_trip is zero; the object is
 *               not modified in either case.
 *
 * @pre  @p db is not NULL.
 * @pre  @p rise_trip is greater than zero.
 *
 * @post On success: `db->trip == rise_trip`, `db->fall_trip == fall_trip`,
 *       `db->counter == 0`, `db->fall_counter == 0`,
 *       `db->output == false`, `db->latch == false`,
 *       `db->prev_output == false`, `db->enabled == true`.
 * @post On failure: the object pointed to by @p db is not modified.
 *
 * @note  Equivalent to debounce_init() when @p fall_trip is zero.
 *
 * @note  MISRA C 2012 Rule 17.7 — the return value shall be used by the
 *        caller.  On safety-critical targets, a false return should be
 *        treated as a configuration fault.
 */
static inline bool
debounce_init_symmetric(struct debounce *db, uint16_t rise_trip,
                        uint16_t fall_trip)
{
        bool result = false;

        if ((db != NULL) && (rise_trip != 0u)) {
                db->trip         = rise_trip;
                db->counter      = 0u;
                db->fall_trip    = fall_trip;
                db->fall_counter = 0u;
                db->output       = false;
                db->latch        = false;
                db->enabled      = true;
                db->prev_output  = false;
#if DEBOUNCE_ENABLE_CALLBACKS
                db->callback = NULL;
#endif
                result = true;
        }

        return result;
}

/**
 * @brief Set the de-assertion (fall) trip threshold at runtime.
 *
 * @param db         Pointer to the debounce object (must be non-NULL).
 * @param fall_trip  New de-assertion threshold.  Zero means immediate
 *                   de-assertion (one-sided behaviour).
 *
 * @return  true on success; false if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init()
 *       or debounce_init_symmetric().
 *
 * @post `db->fall_trip == fall_trip`, `db->fall_counter == 0`.
 * @post All other fields are unchanged.
 */
static inline bool
debounce_set_fall_trip(struct debounce *db, uint16_t fall_trip)
{
        bool result = false;

        if (db != NULL) {
                db->fall_trip    = fall_trip;
                db->fall_counter = 0u;
                result           = true;
        }

        return result;
}

/**
 * @brief Query the configured de-assertion (fall) trip threshold.
 *
 * @param db  Pointer to the debounce object (must be non-NULL).
 *
 * @return  The fall trip threshold; 0 if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init()
 *       or debounce_init_symmetric().
 *
 * @post The object is not modified.
 */
static inline uint16_t
debounce_get_fall_trip(const struct debounce *db)
{
        uint16_t result = 0u;

        if (db != NULL) {
                result = db->fall_trip;
        }

        return result;
}

/* ── runtime trip reconfiguration ───────────────────────────────────────── */

/**
 * @brief Change the assertion trip threshold at runtime.
 *
 * @param db    Pointer to the debounce object (must be non-NULL).
 * @param trip  New assertion threshold.  Must be greater than zero.
 *
 * @return  true on success; false if @p db is NULL or @p trip is zero.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init()
 *       or debounce_init_symmetric().
 * @pre  @p trip is greater than zero.
 *
 * @post On success: `db->trip == trip`, `db->counter == 0`,
 *       `db->output == false`.  `db->latch`, `db->enabled`,
 *       `db->prev_output`, `db->fall_trip`, and `db->fall_counter`
 *       are unchanged.
 * @post On failure: the object is not modified.
 *
 * @note  The counter and output are reset because the old counter value
 *        may violate the invariant `counter <= trip` under the new
 *        threshold.  The sticky latch is preserved (consistent with
 *        debounce_disable semantics).
 */
static inline bool
debounce_set_trip(struct debounce *db, uint16_t trip)
{
        bool result = false;

        if ((db != NULL) && (trip != 0u)) {
                db->trip    = trip;
                db->counter = 0u;
                db->output  = false;
                result      = true;
        }

        return result;
}

/* ── edge detection ─────────────────────────────────────────────────────── */

/**
 * @brief Query whether a rising edge occurred on the last update.
 *
 * @param db  Pointer to the debounce object (must be non-NULL).
 *
 * @return  true if the debounced output transitioned from false to true
 *          on the most recent debounce_update() call; false otherwise
 *          or if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init()
 *       or debounce_init_symmetric().
 *
 * @post The object is not modified.
 */
static inline bool
debounce_rose(const struct debounce *db)
{
        bool result = false;

        if (db != NULL) {
                result = (db->output && !db->prev_output);
        }

        return result;
}

/**
 * @brief Query whether a falling edge occurred on the last update.
 *
 * @param db  Pointer to the debounce object (must be non-NULL).
 *
 * @return  true if the debounced output transitioned from true to false
 *          on the most recent debounce_update() call; false otherwise
 *          or if @p db is NULL.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init()
 *       or debounce_init_symmetric().
 *
 * @post The object is not modified.
 */
static inline bool
debounce_fell(const struct debounce *db)
{
        bool result = false;

        if (db != NULL) {
                result = (!db->output && db->prev_output);
        }

        return result;
}

/* ── transition callback ────────────────────────────────────────────────── */

#if DEBOUNCE_ENABLE_CALLBACKS
/**
 * @brief Register or clear a transition callback.
 *
 * @param db  Pointer to the debounce object (must be non-NULL).
 * @param cb  Callback to invoke on rising/falling edges, or NULL to
 *            disable callbacks.
 *
 * @pre  @p db is not NULL.
 * @pre  @p db has been successfully initialised with debounce_init()
 *       or debounce_init_symmetric().
 *
 * @post `db->callback == cb`.
 * @post All other fields are unchanged.
 *
 * @note  The callback is configuration, not transient state: it is
 *        preserved across debounce_reset() and debounce_disable().
 * @note  Only available when DEBOUNCE_ENABLE_CALLBACKS is defined to 1.
 */
static inline void
debounce_set_callback(struct debounce *db, debounce_callback_t cb)
{
        if (db != NULL) {
                db->callback = cb;
        }
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* DEBOUNCE_H_ */
