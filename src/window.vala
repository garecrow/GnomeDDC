using Gee;

namespace GnomeDDC {
[GtkTemplate (ui = "/org/gnome/GnomeDDC/ui/window.ui")]
public class Window : Adw.ApplicationWindow {
    [GtkChild] private unowned Gtk.ListView monitor_list;
    [GtkChild] private unowned Gtk.Button refresh_button;
    [GtkChild] private unowned Adw.NavigationSplitView split_view;
    [GtkChild] private unowned Adw.NavigationPage details_page;
    [GtkChild] private unowned Adw.ToolbarView detail_view;
    [GtkChild] private unowned Adw.StatusPage empty_state;

    private Settings settings;
    private Ddc.Service service;
    private GLib.ListStore monitor_store;
    private Gtk.SingleSelection monitor_selection;
    private bool initial_load_completed = false;

    private static struct CategoryDefinition {
        public string id;
        public string title;
        public string description;
    }

    private CategoryDefinition[] categories = {
        { "luminance", "Brightness", "Adjust luminance and backlight levels." },
        { "contrast", "Contrast", "Modify contrast and sharpness controls." },
        { "color", "Color", "Tune color temperature and channel balance." },
        { "power", "Power", "Control power-saving and energy features." },
        { "input", "Inputs", "Switch between monitor inputs." },
        { "advanced", "Advanced", "All remaining monitor capabilities." }
    };

    public Window(Application app, Settings settings) {
        Object(application: app);
        this.settings = settings;
        this.service = new Ddc.Service(settings);

        monitor_store = new GLib.ListStore(typeof(Ddc.Monitor));
        monitor_selection = new Gtk.SingleSelection(monitor_store);
        monitor_selection.autoselect = true;
        monitor_selection.can_unselect = false;

        setup_monitor_list();
        connect_signals();
        queue_refresh();
    }

    private void setup_monitor_list() {
        var factory = new Gtk.SignalListItemFactory();
        factory.setup.connect((item) => {
            var row = new Widgets.MonitorRow();
            item.set_child(row);
        });
        factory.bind.connect((item) => {
            var row = (Widgets.MonitorRow) item.get_child();
            var monitor = (Ddc.Monitor) item.get_item();
            row.update_from_monitor(monitor);
        });

        monitor_list.factory = factory;
        monitor_list.model = monitor_selection;
    }

    private void connect_signals() {
        refresh_button.clicked.connect(() => {
            queue_refresh();
        });

        service.monitors_changed.connect((list) => {
            update_monitor_store(list);
        });

        service.service_unavailable.connect((reason) => {
            show_error_state(reason);
        });

        monitor_selection.selection_changed.connect((position, n_items) => {
            var obj = monitor_selection.get_selected_item();
            if (obj is Ddc.Monitor monitor) {
                settings.set_string("last-monitor", monitor.id);
                show_monitor(monitor);
            } else {
                show_empty_state();
            }
        });
    }

    private void queue_refresh() {
        refresh_button.sensitive = false;
        empty_state.title = "Loading monitors";
        empty_state.description = "Requesting capabilities from ddcutil-service.";
        detail_view.content = empty_state;

        service.refresh_monitors.begin((obj, res) => {
            try {
                service.refresh_monitors.end(res);
                initial_load_completed = true;
            } catch (Error e) {
                show_error_state(e.message);
            }
            refresh_button.sensitive = true;
        });
    }

    private void update_monitor_store(ArrayList<Ddc.Monitor> monitors) {
        while (monitor_store.get_n_items() > 0) {
            monitor_store.remove(0);
        }
        foreach (var monitor in monitors) {
            monitor_store.append(monitor);
        }

        if (monitor_store.get_n_items() == 0) {
            show_empty_state();
            return;
        }

        string preferred_id = settings.get_string("last-monitor");
        bool restored = false;
        if (preferred_id != "") {
            for (uint i = 0; i < monitor_store.get_n_items(); i++) {
                var monitor = (Ddc.Monitor) monitor_store.get_item(i);
                if (monitor.id == preferred_id) {
                    monitor_selection.selected = i;
                    restored = true;
                    break;
                }
            }
        }

        if (!restored) {
            monitor_selection.selected = 0;
        }
    }

    private void show_monitor(Ddc.Monitor monitor) {
        var page = new Adw.PreferencesPage();
        foreach (var category in categories) {
            var features = monitor.features_for_category(category.id);
            if (category.id == "input" && monitor.inputs.size > 0) {
                var group = new Adw.PreferencesGroup();
                group.title = category.title;
                group.description = category.description;
                foreach (var option in monitor.inputs) {
                    var row = new Widgets.InputSourceRow();
                    row.bind_option(option);
                    row.activate_source.connect((choice) => {
                        service.request_input_switch(monitor, choice);
                    });
                    group.add(row);
                }
                page.add(group);
                continue;
            }

            if (features.size == 0) {
                continue;
            }

            var feature_group = new Adw.PreferencesGroup();
            feature_group.title = category.title;
            feature_group.description = category.description;

            foreach (var feature in features) {
                switch (feature.kind) {
                case Ddc.FeatureKind.SLIDER:
                    var slider = new Widgets.FeatureSlider();
                    slider.bind_feature(feature);
                    slider.value_changed.connect((value) => {
                        service.request_value_change(monitor, feature, value);
                    });
                    feature_group.add(slider);
                    break;
                case Ddc.FeatureKind.TOGGLE:
                    var toggle = new Widgets.FeatureToggle();
                    toggle.bind_feature(feature);
                    toggle.toggled.connect((active) => {
                        service.request_toggle(monitor, feature, active);
                    });
                    feature_group.add(toggle);
                    break;
                case Ddc.FeatureKind.CHOICE:
                    var combo_row = new Adw.ComboRow();
                    combo_row.title = feature.name;
                    combo_row.subtitle = feature.description;
                    var model = new Gtk.StringList(null);
                    var values = new ArrayList<int>();
                    foreach (var option in feature.choices) {
                        model.append(option.label);
                        values.add(option.value);
                    }
                    combo_row.model = model;
                    combo_row.selected = find_choice_index(feature, values);
                    combo_row.notify["selected"].connect(() => {
                        int index = (int) combo_row.selected;
                        if (index >= 0 && index < values.size) {
                            service.request_value_change(monitor, feature, values[index]);
                        }
                    });
                    feature_group.add(combo_row);
                    break;
                case Ddc.FeatureKind.COMMAND:
                    var command_row = new Adw.ActionRow();
                    command_row.title = feature.name;
                    command_row.subtitle = feature.description;
                    var button = Gtk.Button.with_label("Run");
                    button.add_css_class("suggested-action");
                    button.clicked.connect(() => {
                        service.request_value_change(monitor, feature, feature.default_value != 0 ? feature.default_value : 1);
                    });
                    command_row.add_suffix(button);
                    command_row.activatable_widget = button;
                    feature_group.add(command_row);
                    break;
                }
            }

            page.add(feature_group);
        }

        if (page.get_n_groups() == 0) {
            var placeholder = new Adw.PreferencesGroup();
            placeholder.title = "No configurable features";
            placeholder.description = "The selected monitor did not report any writable capabilities.";
            page.add(placeholder);
        }

        detail_view.content = page;
        details_page.title = monitor.display_name;
    }

    private uint find_choice_index(Ddc.Feature feature, ArrayList<int> values) {
        for (uint i = 0; i < values.size; i++) {
            if (values[(int) i] == feature.value) {
                return i;
            }
        }
        return 0;
    }

    private void show_empty_state() {
        if (!initial_load_completed) {
            empty_state.title = "Loading monitors";
            empty_state.description = "Requesting capabilities from ddcutil-service.";
        } else {
            empty_state.title = "No monitors detected";
            empty_state.description = "ddcutil-service did not report any compatible displays.";
        }
        detail_view.content = empty_state;
    }

    private void show_error_state(string reason) {
        empty_state.title = "Service unavailable";
        empty_state.description = reason;
        detail_view.content = empty_state;
        while (monitor_store.get_n_items() > 0) {
            monitor_store.remove(0);
        }
    }
}
}
