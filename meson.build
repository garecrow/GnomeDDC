project('gnomeddc', 'c',
  version: '0.1.0',
  meson_version: '>=1.2.0',
  default_options: ['warning_level=2', 'c_std=gnu11']
)

adw_dep = dependency('libadwaita-1', version: '>=1.4')
gio_dep = dependency('gio-2.0')
glib_dep = dependency('glib-2.0')
gobject_dep = dependency('gobject-2.0')
gtk_dep = dependency('gtk4', version: '>=4.10')

subdir('data')
subdir('src')
