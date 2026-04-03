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
