gnome = import('gnome')

resources = gnome.compile_resources(
  'gnomeddc-resources',
  '../data/resources/gnomeddc.gresource.xml',
  source_dir: '../data/resources',
  c_name: 'gnomeddc'
)

sources = [
  'main.c',
  'gnomeddc-application.c',
  'gnomeddc-window.c',
  'gnomeddc-client.c',
  'gnomeddc-display.c',
]

executable('gnomeddc',
  sources + resources,
  dependencies: [adw_dep, gio_dep, glib_dep, gobject_dep, gtk_dep],
  install: true
)
