# debounce

Generic saturating counter debounce primitive.

## Features

- **Saturating counter** : Consecutive-assertion tick count that saturates at the configured threshold and never overflows.
- **Dual output model** : A non-sticky `output` that tracks the debounced input, and a separate sticky `latch` that holds until explicitly cleared, suited to fault-hold and operator-acknowledgement patterns.
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

## API Reference

### Struct

```c
struct debounce {
    uint16_t trip;    /* Configured assertion threshold; set by debounce_init() */
    uint16_t counter; /* Consecutive-assertion tick count in [0, trip]          */
    bool     output;  /* Current debounced output                                */
    bool     latch;   /* Sticky latch; cleared by debounce_clear_latch()         */
    bool     enabled; /* Processing gate; controlled by enable/disable           */
};
```

Do not access struct fields directly. Use the API functions below to preserve invariants.

---

### Lifecycle

| Function | Description |
|---|---|
| `debounce_init(db, trip)` | Initialise the object. Returns `true` on success. Returns `false` (without modifying the object) if `db` is NULL or `trip` is zero. On success: sets `trip`, zeroes all other fields, sets `enabled = true`. |
| `debounce_reset(db)` | Reset `counter`, `output`, and `latch` to zero. Preserves `trip` and `enabled`. |

---

### Core processing

| Function | Description |
|---|---|
| `debounce_update(db, cond)` | Process one tick. Returns the current debounced output. When `cond` clears, `counter` and `output` reset immediately; the sticky `latch` is not cleared. When not enabled, this is a no-op returning `false`. |

---

### State queries

| Function | Returns | Description |
|---|---|---|
| `debounce_is_active(db)` | `bool` | Current debounced output. Non-destructive; does not process a tick. |
| `debounce_is_latched(db)` | `bool` | Sticky latch state. True once output has been asserted; remains true until `debounce_clear_latch()` or `debounce_reset()`. |
| `debounce_get_counter(db)` | `uint16_t` | Current consecutive-assertion tick count. |
| `debounce_get_trip(db)` | `uint16_t` | Configured trip threshold. |
| `debounce_is_enabled(db)` | `bool` | Whether the processing gate is open. |

---

### Sticky latch

| Function | Description |
|---|---|
| `debounce_clear_latch(db)` | Clear the sticky latch. If the debounced output is currently active, the latch will be re-set on the next `debounce_update()` call. |

---

### Enable / disable gate

| Function | Description |
|---|---|
| `debounce_enable(db)` | Open the processing gate. No other state is modified. |
| `debounce_disable(db)` | Close the processing gate and clear `counter` and `output`. The sticky `latch` is preserved so a fault record is not silently discarded. |

---

### Defensive behaviour

All functions accept a `NULL` `db` pointer and return a safe default (`false` or `0`) without crashing. `debounce_update()` also returns `false` safely when `trip == 0`.

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
| Sticky latch and disable | `debounce_disable()` intentionally preserves the sticky latch. This prevents a disable/enable cycle from silently discarding a fault record. Call `debounce_clear_latch()` explicitly when acknowledgement is appropriate. |
| Re-arm after disable | `debounce_disable()` clears `counter` and `output`, so calling `debounce_enable()` afterwards gives a clean starting state without requiring a separate `debounce_reset()`. |
| MISRA deviations | Rule 15.5 (advisory): each function has a single point of exit via a `result` variable. This is the only documented deviation. |
| Thread safety | The library provides no synchronisation. If a `struct debounce` is shared across contexts (e.g. ISR and task), the caller is responsible for appropriate access protection. |
