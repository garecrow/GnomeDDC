using Gee;

namespace GnomeDDC.Ddc {
public enum FeatureKind {
    SLIDER,
    TOGGLE,
    CHOICE,
    COMMAND
}

public class FeatureChoice : Object {
    public int value { get; construct; }
    public string label { get; construct; }

    public FeatureChoice(int value, string label) {
        Object(value: value, label: label);
    }
}

public class Feature : Object {
    public string identifier { get; construct; }
    public string name { get; construct; }
    public string description { get; construct; }
    public FeatureKind kind { get; construct; }
    public uint8 code { get; construct; }
    public string category { get; construct; default = "general"; }
    public int min_value { get; set; default = 0; }
    public int max_value { get; set; default = 100; }
    public int step { get; set; default = 1; }
    public int default_value { get; set; }
    public int value { get; set; }
    public bool is_mutable { get; construct; default = true; }
    public bool is_available { get; set; default = true; }
    public Gee.ArrayList<FeatureChoice> choices { get; private set; }

    public signal void value_changed(int new_value);
    public signal void toggled(bool enabled);

    public Feature(string identifier, string name, string description, FeatureKind kind, uint8 code, string category = "general") {
        Object(identifier: identifier, name: name, description: description, kind: kind, code: code, category: category);
        choices = new Gee.ArrayList<FeatureChoice>();
    }

    public void update_value(int new_value) {
        value = new_value;
        value_changed(new_value);
    }

    public void update_toggle(bool enabled) {
        value = enabled ? 1 : 0;
        toggled(enabled);
    }

    public void set_choices(Gee.ArrayList<FeatureChoice> new_choices) {
        choices = new_choices;
    }
}
}
