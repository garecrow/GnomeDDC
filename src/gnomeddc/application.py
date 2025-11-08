from __future__ import annotations

from gi.repository import Adw, Gdk, Gio, GLib, Gtk

from .window import GnomeDdcWindow


class GnomeDdcApplication(Adw.Application):
    """Main GNOME application entry point."""

    def __init__(self) -> None:
        super().__init__(
            application_id='io.github.gnomeddc',
            flags=Gio.ApplicationFlags.FLAGS_NONE,
        )
        self.create_action('quit', self._on_quit, ['<primary>q'])
        self.create_action('refresh', self._on_refresh, ['<primary>r'])
        self.create_action('about', self._on_about)

    def do_startup(self) -> None:  # type: ignore[override]
        Adw.Application.do_startup(self)
        self._load_css()

    def do_activate(self) -> None:  # type: ignore[override]
        window = self.props.active_window
        if not window:
            window = GnomeDdcWindow(application=self)
        window.present()

    def _load_css(self) -> None:
        provider = Gtk.CssProvider()
        try:
            provider.load_from_resource('/io/github/gnomeddc/styles/gnomeddc.css')
        except GLib.Error:
            return
        display = Gdk.Display.get_default()
        if display is None:
            return
        Gtk.StyleContext.add_provider_for_display(display, provider, Gtk.STYLE_PROVIDER_PRIORITY_APPLICATION)

    def _on_quit(self, _action: Gio.SimpleAction, _param: Gio.Variant | None) -> None:
        self.quit()

    def _on_refresh(self, _action: Gio.SimpleAction, _param: Gio.Variant | None) -> None:
        window = self.props.active_window
        if isinstance(window, GnomeDdcWindow):
            window.reload_monitors()

    def _on_about(self, _action: Gio.SimpleAction, _param: Gio.Variant | None) -> None:
        about = Adw.AboutDialog(
            application_name='Gnome DDC',
            application_icon='io.github.gnomeddc',
            developer_name='Gnome DDC Contributors',
            version='0.1.0',
            issue_url='https://github.com/gnomeddc/gnomeddc/issues',
            website='https://github.com/gnomeddc/gnomeddc',
            license_type=Gtk.License.GPL_3_0,
        )
        about.add_acknowledgement_section('Powered by', ['ddcutil-service'])
        about.present(self.props.active_window)

    def create_action(self, name: str, callback, shortcuts: list[str] | None = None) -> None:
        action = Gio.SimpleAction.new(name, None)
        action.connect('activate', callback)
        self.add_action(action)
        if shortcuts:
            self.set_accels_for_action(f'app.{name}', shortcuts)


