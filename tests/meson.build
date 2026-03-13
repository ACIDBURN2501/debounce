test_exe = executable(
  'test_debounce',
  files('test_debounce.c'),
  dependencies: [debounce_dep],
  include_directories: [public_headers, generated_headers],
)

test('debounce module', test_exe)
