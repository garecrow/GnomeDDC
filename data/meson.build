i18n = import('i18n')

gnome = import('gnome')

app_id = 'org.gnome.GnomeDDC'

desktop_file = i18n.merge_file(
  input: 'org.gnome.GnomeDDC.desktop.in',
  output: '@0@.desktop'.format(app_id),
  po_dir: meson.project_source_root() / 'po',
  install: true,
  install_dir: get_option('datadir') / 'applications'
)

appdata_file = i18n.merge_file(
  input: 'org.gnome.GnomeDDC.appdata.xml.in',
  output: '@0@.appdata.xml'.format(app_id),
  po_dir: meson.project_source_root() / 'po',
  install: true,
  install_dir: get_option('datadir') / 'metainfo'
)

gsettings_schema = gnome.compile_schemas()

install_data('icons/org.gnome.GnomeDDC.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
  install_tag: 'runtime')
