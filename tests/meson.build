thread_dep = dependency('threads')

test_exe = executable(
  'test_debounce',
  files('test_debounce.c'),
  dependencies: [debounce_dep],
  include_directories: [public_headers, generated_headers],
)

test('debounce module', test_exe)

test_callbacks_exe = executable(
  'test_debounce_callbacks',
  files('test_debounce_callbacks.c'),
  dependencies: [debounce_dep],
  include_directories: [public_headers, generated_headers],
  c_args: ['-DDEBOUNCE_ENABLE_CALLBACKS=1'],
)

test('debounce callbacks', test_callbacks_exe)

# The concurrency test is the one that exercises shared-state access from two
# threads, so it is the meaningful place to run ThreadSanitizer.  TSAN is opt-in
# (-Dtsan=true) because it requires libtsan and is x86/host-only; the C2000
# target never builds the test suite.
concurrency_override = []
if get_option('tsan')
  concurrency_override = ['b_sanitize=thread']
endif

test_concurrency_exe = executable(
  'test_debounce_concurrency',
  files('test_debounce_concurrency.c'),
  dependencies: [debounce_dep, thread_dep],
  include_directories: [public_headers, generated_headers],
  override_options: concurrency_override,
)

test('debounce concurrency', test_concurrency_exe)
