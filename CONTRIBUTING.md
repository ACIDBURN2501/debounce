# Contributing to debounce

debounce is a small, header-only C library providing a deterministic
saturating-counter debounce primitive. It is designed to be safe to drop
into interrupt handlers, periodic control loops, and audited embedded
firmware.

## Getting started

The same commands CI runs, locally:

```sh
# Configure with tests + sanitisers (CI default)
meson setup build --buildtype=debug -Dbuild_tests=true \
                  -Db_sanitize=address,undefined
meson compile -C build
meson test -C build --verbose

# ThreadSanitizer (concurrency test, c11 mode — must stay race-free)
meson setup build_tsan --buildtype=debug -Dbuild_tests=true -Dtsan=true
meson compile -C build_tsan
meson test -C build_tsan --verbose

# Coverage
meson setup build_cov --buildtype=debug -Dbuild_tests=true -Db_coverage=true
meson compile -C build_cov && meson test -C build_cov
gcovr --root . --filter '.*debounce\.h' --print-summary
```

## Source style

- `.clang-format` is mandatory. Run `clang-format -i` on every modified
  `.c` / `.h` file before submitting.
- 8-space indent, Linux brace style, 80-column limit. Match the existing
  conventions; do not reformat unrelated code.
- The Meson build system is the single source of truth. Update
  `meson.build` / `tests/meson.build` when adding or removing source files.
- No CMake, no Make, no other build systems.

## C language rules

- C11 only (uses `_Static_assert` and, in the default mode, `_Atomic`).
- The library is **header-only**: all functions are `static inline` in
  `include/debounce.h`. There is no `.c` to compile.
- Use fixed-width types from `<stdint.h>` and `<stdbool.h>`. Never plain
  `int` for counters or thresholds.
- No heap allocation (`malloc`, `free`, VLAs), no recursion, no
  data-dependent loop bounds. All state lives in the caller-owned
  `struct debounce`.
- Validate pointer arguments at every public-API boundary; return a safe
  default (`false` / `0`) on a NULL object.
- Public functions return `bool` (or a value), never `errno`. Mutators
  that can be misconfigured return `false` (e.g. `trip == 0`).
- Single point of exit per function via a `result` variable (the one
  intentional MISRA Rule 15.5 advisory deviation).

## Concurrency

The supported contract is **single-writer / many-readers** (see the
concurrency section of `debounce.h`):

- One context may call the mutating functions on an object; any number of
  contexts may call the single-field read-only queries.
- The atomicity modes make _individual_ field accesses well-defined; they
  do **not** make `debounce_update()` atomic as a whole. Two mutating
  contexts must be serialised by the caller.
- `c11` mode must remain ThreadSanitizer-clean. `volatile` mode is correct
  only on single-core targets and is expected to report races under TSAN
  on a multi-core host — do not "fix" that by changing the algorithm.

## MISRA C:2023 awareness

The library is written with MISRA C:2023 in mind (not formally certified).
The deviation record lives in the `debounce.h` file header. If your change
introduces a new deviation:

1. Add it to the header deviation note (rule, site, justification).
2. Add an inline `/* MISRA <rule> */` marker at the deviation site.
3. Justify the deviation in the PR description.

Required-rule deviations face a higher bar than advisory ones. We currently
have a single advisory deviation (Rule 15.5) and want to keep it that way.

## Tests and coverage

- Add a test for every bug fix and every new feature.
- Tests live in `tests/test_*.c`. `tests/test_installed_consumer.c` is the
  install/packaging smoke test exercised by CI against the installed
  headers — keep it building.
- New code must build and pass in **both** atomicity modes
  (`-Datomicity_mode=c11` and `-Datomicity_mode=volatile`).
- The concurrency test must stay clean under `-Dtsan=true` in c11 mode.
- CI enforces a coverage gate on `include/`: **100% line and 100% branch**
  (`gcovr --fail-under-line 100 --fail-under-branch 100`). New code without
  tests will fail the gate. Run it locally with the Coverage commands above.

## API stability

- `struct debounce` is caller-owned but opaque by contract: use the API
  functions, never touch fields directly. Its layout may change between
  minor versions (e.g. atomicity qualifiers).
- The public function signatures in `debounce.h` are stable across the
  v1.x line.
- Breaking changes go in a new major release and require a deprecation
  note in `CHANGELOG.md`.

## Commits

Use Conventional Commits:

- `feat: ...` new feature
- `fix: ...` bug fix
- `doc: ...` documentation only
- `test: ...` test-only changes
- `chore: ...` build, CI, release work
- `refactor: ...` code change that neither fixes a bug nor adds a feature

Keep the subject under ~70 characters. Use the body to explain _why_ the
change is needed, not _what_ the diff already shows.

## Pull requests

- Open an issue first for non-trivial changes so the design can be agreed
  before implementation.
- Keep PRs focused. One feature or one fix per PR.
- All CI checks must pass: tests on Linux + macOS, ASan + UBSan,
  ThreadSanitizer, the `volatile`-mode build, and the release build.
- Any new MISRA deviation must be flagged in the PR description.

## When in doubt

Open an issue and discuss before writing code. The library is small enough
that even modest design changes have outsized implications.
