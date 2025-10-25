#pragma once

#include <glib.h>

typedef struct {
    gchar *display_id;
    gchar *name;
    gchar *bus;
    gchar *serial;
} DdcutilMonitor;

typedef struct {
    gchar code[3];
    gint current;
    gint maximum;
    gboolean available;
} DdcutilVcpValue;

GPtrArray *ddcutil_list_monitors(GError **error);
gboolean ddcutil_get_brightness(const gchar *display_id, gint *current, gint *maximum, GError **error);
gboolean ddcutil_set_brightness(const gchar *display_id, gint value, GError **error);
gboolean ddcutil_get_vcp_value(const gchar *display_id,
                               const gchar *vcp_code,
                               gint *current,
                               gint *maximum,
                               GError **error);
gboolean ddcutil_set_vcp_value(const gchar *display_id, const gchar *vcp_code, gint value, GError **error);
gboolean ddcutil_get_multiple_vcp_values(const gchar *display_id,
                                         const gchar *const *vcp_codes,
                                         guint n_codes,
                                         DdcutilVcpValue **out_values,
                                         GError **error);

void ddcutil_monitor_free(DdcutilMonitor *monitor);
