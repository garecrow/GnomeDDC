gnome = import('gnome')

resources = gnome.compile_resources('gnomeddc-resources',
  '../data/resources.gresource.xml',
  source_dir: '../data',
  c_name: 'gnomeddc_resources'
)

sources = files(
  'main.vala',
  'application.vala',
  'window.vala',
  'models.vala',
  'ddc/service.vala',
  'ddc/monitor.vala',
  'ddc/feature.vala',
  'ddc/async_queue.vala',
  'widgets/monitor_row.vala',
  'widgets/feature_slider.vala',
  'widgets/feature_toggle.vala',
  'widgets/input_source_row.vala'
)

executable('gnomeddc',
  sources + resources,
  dependencies: [adw_dep, gtk_dep, gio_dep, gee_dep],
  vala_args: ['--target-glib=2.76', '--pkg', 'gio-unix-2.0', '--pkg', 'gee-0.8'],
  install: true
)
