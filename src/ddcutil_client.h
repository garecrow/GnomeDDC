#pragma once

#include <glib.h>

typedef struct {
    gchar *display_id;
    gchar *name;
    gchar *bus;
    gchar *serial;
    gchar *manufacturer;
    gchar *mccs_version;
    gchar *firmware_version;
    gchar *manufacture_date;
} DdcutilMonitor;

typedef struct {
    gboolean success;
    gint current;
    gint maximum;
    gchar *error_message;
} DdcutilVcpValue;

GPtrArray *ddcutil_list_monitors(GError **error);
gboolean ddcutil_get_vcp_value(const gchar *display_id, guint8 code, gint *current, gint *maximum, GError **error);
gboolean ddcutil_set_vcp_value(const gchar *display_id, guint8 code, gint value, GError **error);
gboolean ddcutil_get_brightness(const gchar *display_id, gint *current, gint *maximum, GError **error);
gboolean ddcutil_set_brightness(const gchar *display_id, gint value, GError **error);
gboolean ddcutil_get_multiple_vcp_values(const gchar *display_id,
                                        const guint8 *codes,
                                        guint n_codes,
                                        DdcutilVcpValue *results,
                                        GError **error);

void ddcutil_monitor_free(DdcutilMonitor *monitor);
