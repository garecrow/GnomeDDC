namespace GnomeDDC.Widgets {
[GtkTemplate (ui = "/org/gnome/GnomeDDC/ui/monitor_row.ui")]
public class MonitorRow : Adw.ActionRow {
    [GtkChild] private unowned Gtk.Label status_label;

    public Ddc.Monitor? monitor { get; set; default = null; }

    construct {
        add_css_class("monitor-row");
    }

    public void update_from_monitor(Ddc.Monitor monitor) {
        this.monitor = monitor;
        title = monitor.display_name;
        subtitle = monitor.serial != "" ? monitor.serial : monitor.id;
        if (!monitor.connected) {
            status_label.visible = true;
            status_label.label = "Disconnected";
        } else {
            status_label.visible = false;
        }
    }
}
}
