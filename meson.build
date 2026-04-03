project(
  'debounce',
  'c',
  version: '1.1.0',
  default_options: [
    'c_std=c11',
    'warning_level=3',
  ],
  license: 'MIT',
)

project_description = 'Generic saturating counter debounce primitive.'

ver = meson.project_version().split('.')
major = ver[0].to_int()
minor = ver[1].to_int()
patch = ver[2].to_int()

conf = configuration_data()
conf.set_quoted('VERSION', meson.project_version())
conf.set('MAJOR', major)
conf.set('MINOR', minor)
conf.set('PATCH', patch)

generated_version_header = configure_file(
  input: 'config' / 'debounce_version.h.in',
  output: 'debounce_version.h',
  configuration: conf,
  install: true,
  install_dir: get_option('includedir') / 'debounce',
)

public_headers = include_directories('include')
generated_headers = include_directories('.')

debounce_dep = declare_dependency(
  include_directories: [public_headers, generated_headers],
  sources: generated_version_header,
)

meson.override_dependency('debounce', debounce_dep)

install_headers(
  'include/debounce.h',
  'include/debounce_conf.h',
  subdir: 'debounce',
)

pkg = import('pkgconfig')
pkg.generate(
  name: 'debounce',
  description: project_description,
  filebase: 'debounce',
  version: meson.project_version(),
)

if get_option('build_tests')
  subdir('tests')
endif
