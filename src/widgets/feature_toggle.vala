namespace GnomeDDC.Widgets {
[GtkTemplate (ui = "/org/gnome/GnomeDDC/ui/feature_toggle.ui")]
public class FeatureToggle : Adw.ActionRow {
    [GtkChild] private unowned Gtk.Switch switch_widget;

    public Ddc.Feature? feature { get; private set; }
    public signal void toggled(bool active);

    private bool updating = false;

    construct {
        switch_widget.notify["active"].connect(() => {
            if (updating || feature == null) {
                return;
            }
            toggled(switch_widget.active);
        });
    }

    public void bind_feature(Ddc.Feature feature) {
        this.feature = feature;
        title = feature.name;
        subtitle = feature.description;
        switch_widget.sensitive = feature.is_mutable;
        set_active(feature.value > 0);
        feature.toggled.connect((enabled) => {
            set_active(enabled);
        });
        feature.value_changed.connect((value) => {
            set_active(value > 0);
        });
    }

    private void set_active(bool active) {
        updating = true;
        switch_widget.active = active;
        updating = false;
    }
}
}
