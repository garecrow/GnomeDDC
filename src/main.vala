extern void gnomeddc_resources_register();

public static int main(string[] args) {
    GLib.Intl.setlocale();
    gnomeddc_resources_register();

    var app = new GnomeDDC.Application();
    return app.run(args);
}
