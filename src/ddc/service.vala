using Gee;

namespace GnomeDDC.Ddc {
public class Service : Object {
    public const string DEFAULT_BUS_NAME = "org.ddcutil.DdcutilService";
    public const string DEFAULT_OBJECT_PATH = "/org/ddcutil/DdcutilService";
    public const string DEFAULT_INTERFACE = "org.ddcutil.DdcutilService";

    private Settings settings;
    private DBusProxy? proxy;
    private AsyncQueue queue = new AsyncQueue();
    private ArrayList<Monitor> monitors = new ArrayList<Monitor>();

    public signal void monitors_changed(ArrayList<Monitor> monitors);
    public signal void service_unavailable(string reason);

    public Service(Settings settings) {
        this.settings = settings;
    }

    public async ArrayList<Monitor> refresh_monitors() {
        try {
            var proxy = yield ensure_proxy();
            Variant result = yield call_with_fallback(proxy, { "EnumerateMonitors", "ListMonitors", "EnumerateDisplays" }, null);
            monitors = parse_monitor_list(result);

            foreach (var monitor in monitors) {
                try {
                    Variant capabilities = yield fetch_capabilities(proxy, monitor);
                    populate_monitor_from_capabilities(monitor, capabilities);
                } catch (Error e) {
                    warning("Failed to load capabilities for %s: %s", monitor.display_name, e.message);
                }
            }

            monitors_changed(monitors);
            return monitors;
        } catch (Error e) {
            service_unavailable(e.message);
            throw e;
        }
    }

    public ArrayList<Monitor> get_monitors() {
        return monitors;
    }

    public void request_value_change(Monitor monitor, Feature feature, int value) {
        queue.enqueue((cancellable) => {
            try {
                set_feature_value_sync(monitor, feature, value, cancellable);
            } catch (Error e) {
                warning("Failed to update %s on %s: %s", feature.name, monitor.display_name, e.message);
            }
        });
    }

    public void request_toggle(Monitor monitor, Feature feature, bool enabled) {
        request_value_change(monitor, feature, enabled ? 1 : 0);
    }

    public void request_input_switch(Monitor monitor, FeatureChoice option) {
        queue.enqueue((cancellable) => {
            try {
                set_input_source_sync(monitor, option, cancellable);
            } catch (Error e) {
                warning("Failed to switch input for %s: %s", monitor.display_name, e.message);
            }
        });
    }

    private async DBusProxy ensure_proxy() throws Error {
        if (proxy != null) {
            return proxy;
        }

        var bus_type = settings.get_string("service-bus") == "session" ? BusType.SESSION : BusType.SYSTEM;
        try {
            proxy = yield DBusProxy.new_for_bus(bus_type,
                DBusProxyFlags.NONE,
                null,
                DEFAULT_BUS_NAME,
                DEFAULT_OBJECT_PATH,
                DEFAULT_INTERFACE,
                null);
        } catch (Error e) {
            warning("Unable to create proxy with default parameters: %s", e.message);
            proxy = yield discover_service(bus_type);
        }

        if (proxy == null) {
            throw new IOError.FAILED("Could not locate ddcutil-service on the %s bus".printf(bus_type == BusType.SESSION ? "session" : "system"));
        }

        return proxy;
    }

    private async DBusProxy? discover_service(BusType bus_type) throws Error {
        var connection = yield DBus.get(bus_type);
        Variant names_variant = yield connection.call("org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus", "ListNames", null, new VariantType("(as)"), DBusCallFlags.NONE, -1, null);
        Variant name_list = names_variant.get_child_value(0);
        foreach (Variant name_variant in name_list) {
            string name = name_variant.get_string();
            if (name.down().contains("ddcutil")) {
                try {
                    return yield DBusProxy.new_for_bus(bus_type, DBusProxyFlags.NONE, null, name, DEFAULT_OBJECT_PATH, DEFAULT_INTERFACE, null);
                } catch (Error e) {
                    warning("Failed to create proxy for %s: %s", name, e.message);
                }
            }
        }
        return null;
    }

    private async Variant call_with_fallback(DBusProxy proxy, string[] methods, Variant? parameters) throws Error {
        Error? last_error = null;
        foreach (var method in methods) {
            try {
                return yield proxy.call(method, parameters, DBusCallFlags.NONE, -1, null);
            } catch (Error e) {
                last_error = e;
            }
        }
        if (last_error != null) {
            throw last_error;
        }
        throw new IOError.FAILED("No supported method available");
    }

    private ArrayList<Monitor> parse_monitor_list(Variant variant) {
        if (variant.is_of_type(new VariantType("(aa{sv})"))) {
            variant = variant.get_child_value(0);
        }

        var list = new ArrayList<Monitor>();
        foreach (Variant item in variant) {
            if (!item.is_of_type(new VariantType("a{sv}"))) {
                continue;
            }
            var monitor = parse_monitor_dict(item);
            if (monitor != null) {
                list.add(monitor);
            }
        }
        return list;
    }

    private Monitor? parse_monitor_dict(Variant dict) {
        string id = lookup_string(dict, "id") ?? lookup_string(dict, "bus_address") ?? lookup_string(dict, "path") ?? "unknown";
        string vendor = lookup_string(dict, "manufacturer") ?? lookup_string(dict, "vendor") ?? "";
        string model = lookup_string(dict, "model") ?? lookup_string(dict, "product") ?? "";
        string serial = lookup_string(dict, "serial") ?? lookup_string(dict, "serial_number") ?? "";
        string edid = lookup_string(dict, "edid_hash") ?? lookup_string(dict, "edid") ?? "";
        var monitor = new Monitor(id, vendor, model, serial, edid);
        monitor.connected = lookup_bool(dict, "connected", true);
        monitor.supports_scc = lookup_bool(dict, "supports_scc", false);
        return monitor;
    }

    private async Variant fetch_capabilities(DBusProxy proxy, Monitor monitor) throws Error {
        try {
            return yield proxy.call("GetMonitorCapabilities", new Variant.tuple({ new Variant.string(monitor.id) }), DBusCallFlags.NONE, -1, null);
        } catch (Error e) {
            try {
                return yield proxy.call("GetCapabilities", new Variant.tuple({ new Variant.string(monitor.id) }), DBusCallFlags.NONE, -1, null);
            } catch (Error e2) {
                throw e2;
            }
        }
    }

    private void populate_monitor_from_capabilities(Monitor monitor, Variant capabilities) {
        if (capabilities.is_of_type(new VariantType("(a{sv})"))) {
            capabilities = capabilities.get_child_value(0);
        }
        if (!capabilities.is_of_type(new VariantType("a{sv}"))) {
            return;
        }

        var features_variant = capabilities.lookup_value("features", null);
        if (features_variant != null) {
            parse_features(monitor, features_variant);
        }

        var inputs_variant = capabilities.lookup_value("inputs", null);
        if (inputs_variant != null) {
            parse_inputs(monitor, inputs_variant);
        }
    }

    private void parse_features(Monitor monitor, Variant variant) {
        if (variant.is_of_type(new VariantType("(aa{sv})"))) {
            variant = variant.get_child_value(0);
        }
        foreach (Variant item in variant) {
            if (!item.is_of_type(new VariantType("a{sv}"))) {
                continue;
            }
            try {
                var feature = parse_feature_dict(item);
                if (feature != null) {
                    monitor.upsert_feature(feature);
                }
            } catch (Error e) {
                warning("Failed to parse feature for %s: %s", monitor.display_name, e.message);
            }
        }
    }

    private Feature? parse_feature_dict(Variant dict) throws Error {
        string identifier = lookup_string(dict, "id") ?? lookup_string(dict, "identifier") ?? "unknown";
        string name = lookup_string(dict, "label") ?? lookup_string(dict, "name") ?? identifier;
        string description = lookup_string(dict, "description") ?? "";
        uint8 code = (uint8) lookup_uint(dict, "code", 0);
        string category = lookup_string(dict, "category") ?? categorize_feature(code);
        string kind_value = (lookup_string(dict, "kind") ?? lookup_string(dict, "type") ?? "slider").down();
        FeatureKind kind = parse_feature_kind(kind_value);
        var feature = new Feature(identifier, name, description, kind, code, category);
        feature.min_value = (int) lookup_uint(dict, "min", 0);
        feature.max_value = (int) lookup_uint(dict, "max", 100);
        feature.step = (int) lookup_uint(dict, "step", 1);
        feature.default_value = (int) lookup_uint(dict, "default", feature.min_value);
        feature.value = (int) lookup_uint(dict, "value", feature.default_value);
        feature.is_mutable = lookup_bool(dict, "writable", true);
        feature.is_available = lookup_bool(dict, "available", true);

        var choices_variant = dict.lookup_value("choices", null);
        if (choices_variant != null) {
            feature.set_choices(parse_choices(choices_variant));
        }

        return feature;
    }

    private Gee.ArrayList<FeatureChoice> parse_choices(Variant variant) {
        var list = new Gee.ArrayList<FeatureChoice>();
        if (variant.is_of_type(new VariantType("(aa{sv})"))) {
            variant = variant.get_child_value(0);
        }
        foreach (Variant item in variant) {
            if (!item.is_of_type(new VariantType("a{sv}"))) {
                continue;
            }
            int value = (int) lookup_uint(item, "value", 0);
            string label = lookup_string(item, "label") ?? lookup_string(item, "name") ?? value.to_string();
            list.add(new FeatureChoice(value, label));
        }
        return list;
    }

    private void parse_inputs(Monitor monitor, Variant variant) {
        var inputs = parse_choices(variant);
        monitor.set_inputs(inputs);
    }

    private FeatureKind parse_feature_kind(string kind) {
        switch (kind) {
        case "toggle":
        case "switch":
            return FeatureKind.TOGGLE;
        case "choice":
        case "enum":
        case "menu":
            return FeatureKind.CHOICE;
        case "command":
        case "button":
            return FeatureKind.COMMAND;
        default:
            return FeatureKind.SLIDER;
        }
    }

    private string categorize_feature(uint8 code) {
        if (code == 0x10 || code == 0x12 || code == 0x13) {
            return "luminance";
        }
        if (code == 0x14 || code == 0x16) {
            return "contrast";
        }
        if (code == 0x18 || code == 0x1A || code == 0x1C) {
            return "color";
        }
        if (code == 0x60) {
            return "input";
        }
        if (code == 0xD6 || code == 0xE1) {
            return "power";
        }
        return "advanced";
    }

    private void set_feature_value_sync(Monitor monitor, Feature feature, int value, Cancellable? cancellable) throws Error {
        var proxy = ensure_proxy_sync();
        Error? last_error = null;
        Variant parameters = new Variant.tuple({
            new Variant.string(monitor.id),
            new Variant.byte(feature.code),
            new Variant.uint16((uint16) value.clamp(0, 65535))
        });
        foreach (var method in { "SetFeatureValue", "SetVcpValue", "SetValue" }) {
            try {
                proxy.call_sync(method, parameters, DBusCallFlags.NONE, -1, cancellable);
                Idle.add(() => {
                    feature.update_value(value);
                    return Source.REMOVE;
                });
                return;
            } catch (Error e) {
                last_error = e;
            }
        }
        if (last_error != null) {
            throw last_error;
        }
    }

    private void set_input_source_sync(Monitor monitor, FeatureChoice option, Cancellable? cancellable) throws Error {
        var proxy = ensure_proxy_sync();
        Error? last_error = null;
        Variant parameters = new Variant.tuple({
            new Variant.string(monitor.id),
            new Variant.byte(0x60),
            new Variant.uint16((uint16) option.value)
        });
        foreach (var method in { "SetInputSource", "SetVcpValue", "SelectInput" }) {
            try {
                proxy.call_sync(method, parameters, DBusCallFlags.NONE, -1, cancellable);
                return;
            } catch (Error e) {
                last_error = e;
            }
        }
        if (last_error != null) {
            throw last_error;
        }
    }

    private DBusProxy ensure_proxy_sync() throws Error {
        if (proxy != null) {
            return proxy;
        }
        var loop = new MainLoop();
        DBusProxy? resolved = null;
        Error? error = null;
        ensure_proxy.begin((obj, res) => {
            try {
                resolved = ensure_proxy.end(res);
            } catch (Error e) {
                error = e;
            }
            loop.quit();
        });
        loop.run();
        if (error != null) {
            throw error;
        }
        return resolved;
    }

    private string? lookup_string(Variant dict, string key) {
        var value = dict.lookup_value(key, null);
        if (value == null) {
            return null;
        }
        try {
            return value.get_string();
        } catch (Error e) {
            return null;
        }
    }

    private uint lookup_uint(Variant dict, string key, uint fallback) {
        var value = dict.lookup_value(key, null);
        if (value == null) {
            return fallback;
        }
        try {
            if (value.is_of_type(new VariantType("u"))) {
                return value.get_uint32();
            }
            if (value.is_of_type(new VariantType("q"))) {
                return value.get_uint16();
            }
            if (value.is_of_type(new VariantType("y"))) {
                return value.get_byte();
            }
            if (value.is_of_type(new VariantType("t"))) {
                return (uint) value.get_uint64();
            }
            if (value.is_of_type(new VariantType("n"))) {
                return (uint) value.get_int16();
            }
            if (value.is_of_type(new VariantType("i"))) {
                return (uint) value.get_int32();
            }
        } catch (Error e) {
            warning("Failed to coerce numeric variant for %s: %s", key, e.message);
        }
        return fallback;
    }

    private bool lookup_bool(Variant dict, string key, bool fallback) {
        var value = dict.lookup_value(key, null);
        if (value == null) {
            return fallback;
        }
        try {
            if (value.is_of_type(new VariantType("b"))) {
                return value.get_boolean();
            }
            if (value.is_of_type(new VariantType("u")) || value.is_of_type(new VariantType("q")) || value.is_of_type(new VariantType("y"))) {
                return value.get_uint32() != 0;
            }
        } catch (Error e) {
            warning("Failed to coerce boolean variant for %s: %s", key, e.message);
        }
        return fallback;
    }
}
}
