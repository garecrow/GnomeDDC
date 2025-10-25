#pragma once

#include <glib.h>

typedef struct {
    gchar *display_id;
    gchar *name;
    gchar *bus;
    gchar *serial;
} DdcutilMonitor;

GPtrArray *ddcutil_list_monitors(GError **error);
gboolean ddcutil_get_brightness(const gchar *display_id, gint *current, gint *maximum, GError **error);
gboolean ddcutil_set_brightness(const gchar *display_id, gint value, GError **error);

void ddcutil_monitor_free(DdcutilMonitor *monitor);
