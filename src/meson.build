python = import('python').find_installation('python3')
gnome = import('gnome')

install_subdir('gnomeddc', install_dir: python.get_install_dir())

resources = gnome.compile_resources(
  'io.github.gnomeddc',
  '../data/io.github.gnomeddc.gresource.xml',
  source_dir: '../data',
  c_name: 'io_github_gnomeddc',
  gresource_bundle: true
)

install_data(resources, install_dir: join_paths(python.get_install_dir(), 'gnomeddc'))

configure_file(
  input: 'gnomeddc.in',
  output: 'gnomeddc',
  configuration: {
    'PYTHON': python.path(),
    'PACKAGE': 'gnomeddc'
  },
  install: true,
  install_dir: get_option('bindir'),
  install_mode: 'rwxr-xr-x'
)
