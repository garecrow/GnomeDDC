namespace GnomeDDC {
public class Application : Adw.Application {
    private Settings settings;

    public Application() {
        Object(application_id: "org.gnome.GnomeDDC",
               flags: ApplicationFlags.HANDLES_COMMAND_LINE);
        Adw.init();
        settings = new Settings("org.gnome.GnomeDDC");
    }

    protected override void activate() {
        var window = this.active_window as Window;
        if (window != null) {
            window.present();
            return;
        }

        window = new Window(this, settings);
        window.present();
    }

    protected override int command_line(ApplicationCommandLine command_line) {
        this.activate();
        return 0;
    }
}
}
