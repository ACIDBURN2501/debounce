# debounce

[![CI](https://github.com/ACIDBURN2501/debounce/actions/workflows/ci.yml/badge.svg)](https://github.com/ACIDBURN2501/debounce/actions/workflows/ci.yml)

Generic saturating counter debounce primitive.

## Features

- **Saturating counter** : Consecutive-assertion tick count that saturates at the configured threshold and never overflows.
- **Dual output model** : A non-sticky `output` that tracks the debounced input, and a separate sticky `latch` that holds until explicitly cleared, suited to fault-hold and operator-acknowledgement patterns.
- **Symmetric (two-sided) debounce** : Optionally debounce both assertion and de-assertion edges with independent thresholds via `debounce_init_symmetric()`. When `fall_trip` is zero (the default), de-assertion is immediate (one-sided debounce).
- **Edge detection** : `debounce_rose()` and `debounce_fell()` detect rising and falling transitions, returning `true` for exactly one tick after the output changes.
- **Runtime trip reconfiguration** : `debounce_set_trip()` and `debounce_set_fall_trip()` allow thresholds to be changed at runtime without a full re-init.
- **Transition callbacks** : Optional compile-time feature (`DEBOUNCE_ENABLE_CALLBACKS=1`) for registering a callback invoked on rising and falling edges.
- **Explicit initialisation** : `debounce_init()` validates the trip threshold and brings the object to a defined, clean state; returns `false` on invalid configuration so callers can detect misconfiguration at startup.
- **Enable/disable gate** : `debounce_enable()` and `debounce_disable()` inhibit processing during known transient conditions (e.g. system startup) without discarding the sticky fault record.
- **Allocation-free** : All state is held in a caller-owned `struct debounce`; no dynamic memory, no global state.
- **Deterministic execution** : No recursion, no data-dependent loop bounds, no blocking calls making it safe for use in ISRs and hard real-time control loops.
- **MISRA C 2012 aware** : Single exit points per function, increment separated from comparison, documented advisory deviations making it suitable for IEC-61508 environments.
- **Header-only implementation** : All functions are `static inline`; no separate link step required.

## Using the Library

### As a Meson subproject

```meson
debounce_dep = dependency('debounce', fallback: ['debounce', 'debounce_dep'])
```

The project also exports `meson.override_dependency('debounce', ...)`
so downstream Meson builds can resolve the subproject dependency by name.

For subproject builds, include the public header directly:

```c
#include "debounce.h"
```

### As an installed dependency

If the library is installed system-wide, include the namespaced header path:

```c
#include <debounce/debounce.h>
```

If `pkg-config` files are installed in your environment, downstream builds can
also discover the package as `debounce`.

The generated version header is available as `debounce_version.h` in the
build tree and as `<debounce/debounce_version.h>` after install.

## Building

```sh
# Library only (release)
meson setup build --buildtype=release -Dbuild_tests=false
meson compile -C build

# With unit tests
meson setup build --buildtype=debug -Dbuild_tests=true
meson compile -C build
meson test -C build --verbose
```

## Quick Start

```c
#include "debounce.h"

/* One debounce object per monitored signal, statically allocated. */
static struct debounce pressure_fault;

void system_init(void)
{
    /* Require 10 consecutive asserted ticks before tripping. */
    if (!debounce_init(&pressure_fault, 10u)) {
        /* trip=0 or NULL pointer: handle configuration fault. */
    }
}

void control_loop_tick(bool pressure_over_limit)
{
    debounce_update(&pressure_fault, pressure_over_limit);

    if (debounce_is_active(&pressure_fault)) {
        /* Debounced output is high: condition sustained for 10 ticks. */
        set_warning_led(true);
    }

    if (debounce_is_latched(&pressure_fault)) {
        /* Sticky latch: fault has tripped at least once since last
         * acknowledgement, even if the condition has since cleared. */
        log_fault_event();
    }
}

void operator_acknowledge(void)
{
    /* Explicitly clear the sticky latch on operator action. */
    debounce_clear_latch(&pressure_fault);
}
```

## Output model

The debouncer maintains two independent outputs:

- **`output` (level-triggered):** Tracks the debounced input directly. Goes `true` once
  `trip` consecutive ticks are seen; resets to `false` immediately when the input clears.
  Read via `debounce_update()` or `debounce_is_active()`. No acknowledgement required.
  Suitable for continuously reflecting the clean state of a digital input or voltage
  threshold.

- **`latch` (fault-hold):** Sticky flag set the moment `output` first goes `true`; not
  cleared when the condition clears. Requires an explicit `debounce_clear_latch()` to
  reset. Suitable for fault-hold and operator-acknowledgement patterns.

These are independent. Use `output` alone for level detection without ever touching
`latch`.

## API Reference

### Struct

```c
struct debounce {
    uint16_t trip;         /* Configured assertion threshold                      */
    uint16_t counter;      /* Consecutive-assertion tick count in [0, trip]        */
    uint16_t fall_trip;    /* De-assertion threshold; 0 = immediate (default)      */
    uint16_t fall_counter; /* Consecutive de-asserted tick count in [0, fall_trip] */
    bool     output;       /* Current debounced output                             */
    bool     latch;        /* Sticky latch; cleared by debounce_clear_latch()      */
    bool     enabled;      /* Processing gate; controlled by enable/disable        */
    bool     prev_output;  /* Previous output for edge detection                   */
#if DEBOUNCE_ENABLE_CALLBACKS
    debounce_callback_t callback; /* Optional transition callback                  */
#endif
};
```

Do not access fields directly; use the API functions to preserve invariants.

### Lifecycle

```c
bool debounce_init(struct debounce *db, uint16_t trip);
```

Initialise `db` with the given trip threshold (one-sided debounce: assertion is debounced,
de-assertion is immediate). Returns `true` on success; returns `false` without modifying
the object if `db` is `NULL` or `trip` is zero. On success: sets `trip`, zeroes all
counters, `output`, `latch`, and `prev_output`, and sets `enabled = true`,
`fall_trip = 0`.

```c
bool debounce_init_symmetric(struct debounce *db, uint16_t rise_trip,
                             uint16_t fall_trip);
```

Initialise `db` with separate rise and fall thresholds (two-sided debounce). `rise_trip`
must be greater than zero. `fall_trip` of zero means immediate de-assertion (equivalent
to `debounce_init`). Returns `false` if `db` is `NULL` or `rise_trip` is zero.

```c
void debounce_reset(struct debounce *db);
```

Reset `counter`, `fall_counter`, `output`, `latch`, and `prev_output` to zero/false.
Preserves `trip`, `fall_trip`, `enabled`, and `callback` (if enabled).

### Core processing

```c
bool debounce_update(struct debounce *db, bool cond);
```

Process one tick of the monitored condition. Returns the current debounced output. When
`cond` is true for `trip` consecutive ticks, `output` and `latch` are both set to `true`.
When `cond` clears:
- **One-sided** (`fall_trip == 0`): `counter` and `output` reset immediately.
- **Symmetric** (`fall_trip > 0`): `counter` resets but `output` is held until
  `fall_trip` consecutive de-asserted ticks are seen.

The sticky `latch` is never cleared by this function. When not enabled, this is a no-op
returning `false`. If `DEBOUNCE_ENABLE_CALLBACKS` is set and a callback is registered, it
is invoked on rising and falling edges within this call.

### State queries

```c
bool     debounce_is_active  (const struct debounce *db);
bool     debounce_is_latched (const struct debounce *db);
uint16_t debounce_get_counter(const struct debounce *db);
uint16_t debounce_get_trip   (const struct debounce *db);
bool     debounce_is_enabled (const struct debounce *db);
```

`is_active()` returns the current debounced output without processing a tick.
`is_latched()` returns the sticky latch state; `true` once output has been asserted,
remains `true` until `debounce_clear_latch()` or `debounce_reset()`. `get_counter()` and
`get_trip()` return the current tick count and configured threshold respectively.
`is_enabled()` returns whether the processing gate is open.

### Edge detection

```c
bool debounce_rose(const struct debounce *db);
bool debounce_fell(const struct debounce *db);
```

`rose()` returns `true` for exactly one tick after the debounced output transitions from
false to true. `fell()` returns `true` for exactly one tick after the output transitions
from true to false. With symmetric debounce, `fell()` fires when the output actually
clears (after `fall_trip` de-asserted ticks), not on the first false input. Both return
`false` if `db` is `NULL`. No spurious edges are generated after `debounce_reset()` or a
`debounce_disable()` / `debounce_enable()` cycle.

### Runtime trip reconfiguration

```c
bool debounce_set_trip(struct debounce *db, uint16_t trip);
```

Change the assertion trip threshold at runtime. Returns `false` if `db` is `NULL` or
`trip` is zero. On success: sets `db->trip`, resets `counter` and `output` to
zero/false. The sticky `latch`, `enabled`, `fall_trip`, and `fall_counter` are
unchanged. The counter is reset because the old value may violate the `counter <= trip`
invariant under the new threshold.

```c
bool     debounce_set_fall_trip(struct debounce *db, uint16_t fall_trip);
uint16_t debounce_get_fall_trip(const struct debounce *db);
```

`set_fall_trip()` changes the de-assertion threshold at runtime. `fall_trip` of zero
switches to immediate de-assertion. Resets `fall_counter` to zero. Returns `false` only
if `db` is `NULL`. `get_fall_trip()` returns the current fall threshold; `0` if `db` is
`NULL`.

### Sticky latch

```c
void debounce_clear_latch(struct debounce *db);
```

Clear the sticky latch. If the debounced output is currently active, the latch will be
re-set on the next `debounce_update()` call; ensure the monitored condition has cleared
before calling this.

### Enable / disable gate

```c
void debounce_enable (struct debounce *db);
void debounce_disable(struct debounce *db);
```

`enable()` opens the processing gate without modifying any other state. `disable()` closes
the gate and clears `counter`, `fall_counter`, `output`, and `prev_output`; the sticky
`latch` is intentionally preserved so a fault record is not silently discarded by a
disable/enable cycle.

### Transition callbacks (optional)

```c
/* Requires DEBOUNCE_ENABLE_CALLBACKS=1 in debounce_conf.h or via compiler flag */
typedef void (*debounce_callback_t)(struct debounce *db, bool rose);
void debounce_set_callback(struct debounce *db, debounce_callback_t cb);
```

Register a callback invoked from `debounce_update()` on rising (`rose == true`) and
falling (`rose == false`) edges. Pass `NULL` to disable. The callback is configuration,
not transient state: it is preserved across `debounce_reset()` and `debounce_disable()`.
Only available when `DEBOUNCE_ENABLE_CALLBACKS` is defined to `1`.

### Compile-time configuration

The file `debounce_conf.h` (included automatically by `debounce.h`) provides compile-time
options. Override values before inclusion or via compiler flags:

| Macro | Default | Description |
|-------|---------|-------------|
| `DEBOUNCE_ENABLE_CALLBACKS` | `0` | Set to `1` to enable the transition-callback mechanism. Adds a function-pointer field to `struct debounce` and callback dispatch code in `debounce_update()`. |

### Defensive behaviour

All functions accept a `NULL` `db` pointer and return a safe default (`false` or `0`)
without crashing. `debounce_update()` also returns `false` safely when `trip == 0`.

## Use Cases

- **Mechanical switch / button debounce** : Require N consecutive sampled-true ticks before treating a switch closure as valid, rejecting contact bounce.
- **Digital input noise rejection** : Filter glitches on GPIO lines by demanding a sustained assertion before acting.
- **Fault monitoring with startup inhibit** : Disable the debouncer during power-on transients; enable it once the system has stabilised. The sticky latch records the first fault for later inspection.
- **Operator-acknowledgement fault hold** : Use the sticky latch to hold a fault indicator until an operator explicitly acknowledges it, even if the underlying condition has cleared.
- **Gated monitoring with auto-arm** : Compose two `debounce` objects — one debounces the "system ready" condition and its output gates the second (fault) debouncer. This gives debounced arming without coupling the two concerns into a single object.
- **ISR / hard real-time loops** : The deterministic, allocation-free design makes `debounce_update()` safe to call from interrupt context.

## Notes

| Topic | Note |
|-------|------|
| Trip value | A `trip` of `0` is invalid. All API functions handle it safely and return `false` or `0`, but the debouncer will never activate. Always call `debounce_init()` with a non-zero trip value. |
| Symmetric debounce | `fall_trip` defaults to `0` (immediate de-assertion). Use `debounce_init_symmetric()` or `debounce_set_fall_trip()` for two-sided debounce. A `fall_trip` of `0` is valid and means one-sided behaviour. |
| `debounce_set_trip` | Changing the trip threshold resets `counter` and `output` to preserve the `counter <= trip` invariant. The sticky `latch` is preserved. |
| Edge detection | `debounce_rose()` and `debounce_fell()` are pure queries on `prev_output` captured at the start of each `debounce_update()`. They return `false` if no update has been called, and no spurious edges are generated after `reset` or `disable`/`enable`. |
| Callbacks | Transition callbacks require `DEBOUNCE_ENABLE_CALLBACKS=1` at compile time. When disabled (default), no function pointer is stored and no dispatch code is compiled. The callback is preserved across `debounce_reset()` and `debounce_disable()`. |
| Sticky latch and disable | `debounce_disable()` intentionally preserves the sticky latch. This prevents a disable/enable cycle from silently discarding a fault record. Call `debounce_clear_latch()` explicitly when acknowledgement is appropriate. |
| Re-arm after disable | `debounce_disable()` clears `counter`, `fall_counter`, `output`, and `prev_output`, so calling `debounce_enable()` afterwards gives a clean starting state without requiring a separate `debounce_reset()`. |
| MISRA deviations | Rule 15.5 (advisory): each function has a single point of exit via a `result` variable. This is the only documented deviation. |
| Thread safety | The library provides no synchronisation. If a `struct debounce` is shared across contexts (e.g. ISR and task), the caller is responsible for appropriate access protection. |
