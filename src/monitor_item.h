#pragma once

#include <glib-object.h>

G_BEGIN_DECLS

#define MONITOR_TYPE_ITEM (monitor_item_get_type())

G_DECLARE_FINAL_TYPE(MonitorItem, monitor_item, MONITOR, ITEM, GObject)

MonitorItem *monitor_item_new(const gchar *display_id,
                               const gchar *name,
                               const gchar *bus,
                               const gchar *serial);

const gchar *monitor_item_get_display_id(MonitorItem *self);
const gchar *monitor_item_get_name(MonitorItem *self);
const gchar *monitor_item_get_bus(MonitorItem *self);
const gchar *monitor_item_get_serial(MonitorItem *self);

void monitor_item_set_name(MonitorItem *self, const gchar *name);

G_END_DECLS
