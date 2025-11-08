using Gee;

namespace GnomeDDC {
public class FeatureGroup : Object {
    public string id { get; construct; }
    public string title { get; construct; }
    public string description { get; construct; }
    public ArrayList<Ddc.Feature> features { get; private set; }

    public FeatureGroup(string id, string title, string description) {
        Object(id: id, title: title, description: description);
        features = new ArrayList<Ddc.Feature>();
    }

    public void set_features(ArrayList<Ddc.Feature> list) {
        features = list;
    }
}
}
