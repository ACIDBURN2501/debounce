# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## [1.2.0] - 2026-05-27

### Added

- Configurable atomicity model via `DEBOUNCE_ATOMIC_MODE`, selectable at
  the toolchain level or with the Meson `-Datomicity_mode=` option:
  - `c11` (default): `struct debounce` fields are `_Atomic`
    (`<stdatomic.h>`). Race-free for the single-writer/many-readers
    contract on multi-core hosts and RTOSes; verified clean under
    ThreadSanitizer.
  - `volatile`: `volatile`-qualified fields for single-core MCUs whose
    toolchain ships no `<stdatomic.h>` (e.g. TI C2000), where a
    naturally-aligned word access is a single, indivisible instruction.
- C11 `_Static_assert` checks: `uint16_t` holds at least 16 bits
  (`sizeof * CHAR_BIT`) and `struct debounce` stays within a reasonable
  size bound.
- Documented **single-writer / many-readers** threading contract in the
  `debounce.h` header and README. Edge queries (`debounce_rose()` /
  `debounce_fell()`) are writer-context only.
- `tests/test_debounce_concurrency.c`: two-thread stress test, runnable
  under ThreadSanitizer via the Meson `-Dtsan=true` option.
- CI: dedicated ThreadSanitizer job (c11 mode), a `volatile`-mode build to
  guard the single-core path, and a coverage gate enforcing 100% line and
  100% branch on `include/` via `gcovr`.

### Changed

- `struct debounce` fields are now atomicity-qualified according to the
  selected mode. The struct remains caller-owned; the documented "do not
  access fields directly" rule is unchanged.
- The default mode (`c11`) requires `<stdatomic.h>`. Targets without it
  **must** select `volatile`. Selecting an unsupported
  `DEBOUNCE_ATOMIC_MODE` is now a hard `#error` rather than a silent
  fallback to non-atomic accesses.

## [1.1.0] - 2026-04-03

First tagged release.

### Added

- Symmetric (two-sided) debounce with independent rise/fall thresholds
  via `debounce_init_symmetric()` and `debounce_set_fall_trip()`.
- Edge detection: `debounce_rose()` and `debounce_fell()`.
- Runtime trip reconfiguration: `debounce_set_trip()`.
- Optional transition callbacks (`DEBOUNCE_ENABLE_CALLBACKS=1`).
- Expanded unit-test coverage.

### Core API (from initial development)

- Saturating-counter debounce primitive: `debounce_init()`,
  `debounce_update()`, `debounce_reset()`.
- Dual output model: non-sticky `output` plus a sticky `latch`
  (`debounce_is_active()`, `debounce_is_latched()`,
  `debounce_clear_latch()`).
- Enable/disable processing gate (`debounce_enable()`,
  `debounce_disable()`, `debounce_is_enabled()`).
- Header-only, allocation-free, deterministic; defensive NULL checks at
  every API boundary.
