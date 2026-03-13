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

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ================ STRUCTURES ============================================== */

/**
 * @struct debounce
 * @brief  Saturating-counter debouncer state.
 *
 * Initialise with debounce_init() before first use.  Do not access fields
 * directly; use the provided API functions to preserve invariants.
 *
 * @par Struct invariants (hold after a successful debounce_init() call)
 *   - `counter` is always in the range [0, trip].
 *   - `latch` is only true if `output` has been true at least once since
 *     the last debounce_clear_latch() or debounce_reset() call.
 *   - `trip` is never zero after a successful debounce_init() call.
 *
 * @var trip     Configured assertion threshold (ticks).  Set once by
 *               debounce_init(); never modified by update functions.
 *               A value of 0 is invalid; debounce_init() rejects it.
 *
 * @var counter  Consecutive asserted ticks in the range [0, trip].
 *               Saturates at `trip`; never exceeds it.
 *
 * @var output   Current debounced output.  Becomes true once `counter`
 *               reaches `trip`; resets to false when the input clears.
 *               Read via debounce_is_active().
 *
 * @var latch    Sticky fault latch.  Set whenever `output` becomes true;
 *               NOT cleared when the input clears.  Must be cleared
 *               explicitly with debounce_clear_latch().
 *               Read via debounce_is_latched().
 *
 * @var enabled  Processing gate.  When false, debounce_update() is a no-op
 *               and returns false.  Controlled via debounce_enable() and
 *               debounce_disable().
 */
struct debounce {
        uint16_t trip;
        uint16_t counter;
        bool output;
        bool latch;
        bool enabled;
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
 *       `db->output == false`, `db->latch == false`,
 *       `db->enabled == true`.
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
                db->trip    = trip;
                db->counter = 0u;
                db->output  = false;
                db->latch   = false;
                db->enabled = true;
                result      = true;
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
 * @post `db->counter == 0`, `db->output == false`, `db->latch == false`.
 * @post `db->trip` and `db->enabled` are unchanged.
 *
 * @note  To clear only the sticky latch without resetting the counter or
 *        output, use debounce_clear_latch() instead.
 */
static inline void
debounce_reset(struct debounce *db)
{
        if (db != NULL) {
                db->counter = 0u;
                db->output  = false;
                db->latch   = false;
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
 * @post If @p cond is false: `db->counter == 0` and `db->output == false`.
 * @post `db->latch` is never cleared by this function.
 * @post `db->trip` and `db->enabled` are never modified by this function.
 *
 * @note  When `cond` clears, `counter` and `output` are reset immediately
 *        (one-sided debounce: assertion is debounced, de-assertion is not).
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
                if (cond) {
                        if (db->counter < db->trip) {
                                db->counter++;
                        }
                        if (db->counter >= db->trip) {
                                db->output = true;
                                db->latch  = true;
                        }
                } else {
                        db->counter = 0u;
                        db->output  = false;
                }
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
 * @note  debounce_disable() already clears `counter` and `output`, so
 *        re-enabling after a disable gives a clean starting state without
 *        requiring a separate debounce_reset() call.
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
 * @post `db->enabled == false`, `db->counter == 0`, `db->output == false`.
 * @post `db->latch` is unchanged (intentionally preserved; see note).
 * @post `db->trip` is unchanged.
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
                db->enabled = false;
                db->counter = 0u;
                db->output  = false;
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

#ifdef __cplusplus
}
#endif

#endif /* DEBOUNCE_H_ */
