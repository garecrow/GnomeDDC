namespace GnomeDDC.Widgets {
[GtkTemplate (ui = "/org/gnome/GnomeDDC/ui/feature_slider.ui")]
public class FeatureSlider : Adw.PreferencesRow {
    [GtkChild] private unowned Gtk.Scale scale;
    [GtkChild] private unowned Gtk.SpinButton spin;
    [GtkChild] private unowned Gtk.Button reset_button;

    public Ddc.Feature? feature { get; private set; }
    public signal void value_changed(int value);

    private bool updating = false;

    construct {
        scale.value_changed.connect(() => {
            if (updating || feature == null) {
                return;
            }
            updating = true;
            int value = (int) Math.round(scale.get_value());
            spin.set_value(value);
            value_changed(value);
            updating = false;
        });

        spin.value_changed.connect(() => {
            if (updating || feature == null) {
                return;
            }
            updating = true;
            int value = spin.get_value_as_int();
            scale.set_value(value);
            value_changed(value);
            updating = false;
        });

        reset_button.clicked.connect(() => {
            if (feature == null) {
                return;
            }
            set_value(feature.default_value);
            value_changed(feature.default_value);
        });
    }

    public void bind_feature(Ddc.Feature feature) {
        this.feature = feature;
        title = feature.name;
        tooltip_text = feature.description;
        set_adjustment_bounds(feature);
        set_value(feature.value);
        reset_button.sensitive = feature.is_mutable;
        scale.sensitive = feature.is_mutable;
        spin.sensitive = feature.is_mutable;

        feature.value_changed.connect((value) => {
            set_value(value);
        });
    }

    private void set_adjustment_bounds(Ddc.Feature feature) {
        double lower = feature.min_value;
        double upper = feature.max_value;
        double step = feature.step > 0 ? feature.step : 1;
        var adjustment = new Gtk.Adjustment(feature.value, lower, upper, step, step * 5, 0);
        scale.set_adjustment(adjustment);
        spin.set_adjustment(adjustment);
    }

    private void set_value(int value) {
        updating = true;
        scale.set_value(value);
        spin.set_value(value);
        updating = false;
    }
}
}
