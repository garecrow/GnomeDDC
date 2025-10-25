#include "monitor_item.h"

struct _MonitorItem {
    GObject parent_instance;
    gchar *display_id;
    gchar *name;
    gchar *bus;
    gchar *serial;
    gchar *manufacturer;
    gchar *mccs_version;
    gchar *firmware;
    gchar *manufacture_date;
};

G_DEFINE_TYPE(MonitorItem, monitor_item, G_TYPE_OBJECT)

static void monitor_item_dispose(GObject *object) {
    MonitorItem *self = MONITOR_ITEM(object);
    g_clear_pointer(&self->display_id, g_free);
    g_clear_pointer(&self->name, g_free);
    g_clear_pointer(&self->bus, g_free);
    g_clear_pointer(&self->serial, g_free);
    g_clear_pointer(&self->manufacturer, g_free);
    g_clear_pointer(&self->mccs_version, g_free);
    g_clear_pointer(&self->firmware, g_free);
    g_clear_pointer(&self->manufacture_date, g_free);
    G_OBJECT_CLASS(monitor_item_parent_class)->dispose(object);
}

static void monitor_item_class_init(MonitorItemClass *klass) {
    GObjectClass *object_class = G_OBJECT_CLASS(klass);
    object_class->dispose = monitor_item_dispose;
}

static void monitor_item_init(MonitorItem *self) {
    (void)self;
}

MonitorItem *monitor_item_new(const gchar *display_id,
                               const gchar *name,
                               const gchar *bus,
                               const gchar *serial,
                               const gchar *manufacturer,
                               const gchar *mccs_version,
                               const gchar *firmware,
                               const gchar *manufacture_date) {
    MonitorItem *self = g_object_new(MONITOR_TYPE_ITEM, NULL);
    self->display_id = g_strdup(display_id);
    self->name = g_strdup(name);
    self->bus = g_strdup(bus);
    self->serial = g_strdup(serial);
    self->manufacturer = g_strdup(manufacturer);
    self->mccs_version = g_strdup(mccs_version);
    self->firmware = g_strdup(firmware);
    self->manufacture_date = g_strdup(manufacture_date);
    return self;
}

const gchar *monitor_item_get_display_id(MonitorItem *self) {
    return self->display_id;
}

const gchar *monitor_item_get_name(MonitorItem *self) {
    return self->name;
}

const gchar *monitor_item_get_bus(MonitorItem *self) {
    return self->bus;
}

const gchar *monitor_item_get_serial(MonitorItem *self) {
    return self->serial;
}

const gchar *monitor_item_get_manufacturer(MonitorItem *self) {
    return self->manufacturer;
}

const gchar *monitor_item_get_mccs_version(MonitorItem *self) {
    return self->mccs_version;
}

const gchar *monitor_item_get_firmware(MonitorItem *self) {
    return self->firmware;
}

const gchar *monitor_item_get_manufacture_date(MonitorItem *self) {
    return self->manufacture_date;
}

void monitor_item_set_name(MonitorItem *self, const gchar *name) {
    if (!self) {
        return;
    }

    if (g_strcmp0(self->name, name) == 0) {
        return;
    }

    g_free(self->name);
    self->name = name ? g_strdup(name) : NULL;
}
