namespace GnomeDDC.Widgets {
[GtkTemplate (ui = "/org/gnome/GnomeDDC/ui/input_source_row.ui")]
public class InputSourceRow : Adw.ActionRow {
    [GtkChild] private unowned Gtk.Button activate_button;

    public Ddc.FeatureChoice? option { get; private set; }
    public signal void activate_source(Ddc.FeatureChoice option);

    construct {
        activate_button.clicked.connect(() => {
            if (option != null) {
                activate_source(option);
            }
        });
    }

    public void bind_option(Ddc.FeatureChoice option) {
        this.option = option;
        title = option.label;
        subtitle = "Input code %d".printf(option.value);
    }
}
}
