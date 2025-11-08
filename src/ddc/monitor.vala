using Gee;

namespace GnomeDDC.Ddc {
public class Monitor : Object {
    public string id { get; construct; }
    public string vendor { get; construct; }
    public string model { get; construct; }
    public string serial { get; construct; }
    public string edid_hash { get; construct; }
    public bool connected { get; set; default = true; }
    public bool supports_scc { get; set; default = false; }
    public ArrayList<Feature> features { get; private set; }
    public ArrayList<FeatureChoice> inputs { get; private set; }

    public signal void features_changed();
    public signal void feature_updated(Feature feature);

    public Monitor(string id, string vendor, string model, string serial, string edid_hash) {
        Object(id: id, vendor: vendor, model: model, serial: serial, edid_hash: edid_hash);
        features = new ArrayList<Feature>();
        inputs = new ArrayList<FeatureChoice>();
    }

    public string display_name {
        owned get {
            if (vendor.strip() == "" && model.strip() == "") {
                return id;
            }
            if (vendor.strip() == "") {
                return model;
            }
            if (model.strip() == "") {
                return vendor;
            }
            return "%s %s".printf(vendor, model);
        }
    }

    public void upsert_feature(Feature feature) {
        for (int i = 0; i < features.size; i++) {
            var existing = features[i];
            if (existing.identifier == feature.identifier) {
                features[i] = feature;
                feature_updated(feature);
                features_changed();
                return;
            }
        }
        features.add(feature);
        feature_updated(feature);
        features_changed();
    }

    public Feature? find_feature_by_code(uint8 code) {
        foreach (var feature in features) {
            if (feature.code == code) {
                return feature;
            }
        }
        return null;
    }

    public ArrayList<Feature> features_for_category(string category) {
        var subset = new ArrayList<Feature>();
        foreach (var feature in features) {
            if (feature.category == category) {
                subset.add(feature);
            }
        }
        return subset;
    }

    public void set_inputs(ArrayList<FeatureChoice> options) {
        inputs = options;
        features_changed();
    }
}
}
