#include "gnomeddc-window.h"

#include "gnomeddc-client.h"
#include "gnomeddc-display.h"

#include <glib/gi18n.h>
#include <math.h>

#define DDCUTIL_INTERFACE_NAME "com.ddcutil.DdcutilInterface"

struct _GnomeDdcWindow {
  AdwApplicationWindow parent_instance;

  GnomeDdcClient *client;
  GListStore *display_store;
  GtkCustomFilter *search_filter;
  GtkFilterListModel *filter_model;
  GtkSingleSelection *selection;
  gchar *search_text;
  guint pending_calls;
  gboolean updating_service_properties;

  AdwToastOverlay *toast_overlay;
  GtkListView *display_list;
  GtkSearchEntry *display_search_entry;
  AdwStatusPage *empty_status;
  GtkSpinner *busy_spinner;
  GtkButton *refresh_button;
  GtkButton *list_detected_button;
  GtkButton *detect_button;
  GtkButton *query_state_button;
  GtkButton *sleep_multiplier_refresh_button;
  GtkButton *get_vcp_button;
  GtkButton *get_multiple_vcp_button;
  GtkButton *set_vcp_button;
  GtkButton *set_vcp_context_button;
  GtkButton *get_vcp_metadata_button;
  GtkButton *get_capabilities_button;
  GtkButton *get_capabilities_metadata_button;
  GtkButton *set_sleep_multiplier_button;
  GtkButton *restart_button;

  AdwActionRow *name_row;
  AdwActionRow *model_row;
  AdwActionRow *manufacturer_row;
  AdwActionRow *serial_row;
  AdwActionRow *product_code_row;
  AdwActionRow *display_number_row;
  AdwActionRow *usb_row;
  AdwActionRow *state_row;
  AdwActionRow *sleep_multiplier_row;
  AdwActionRow *get_vcp_row;
  AdwActionRow *get_multiple_vcp_row;
  AdwActionRow *set_vcp_row;
  AdwActionRow *set_vcp_context_row;
  AdwActionRow *get_vcp_metadata_row;
  AdwActionRow *get_capabilities_row;
  AdwActionRow *get_capabilities_metadata_row;
  AdwActionRow *set_sleep_multiplier_row;
  AdwActionRow *service_version_row;
  AdwActionRow *ddcutil_version_row;
  AdwActionRow *service_parameters_locked_row;
  AdwActionRow *attributes_row;
  AdwActionRow *status_values_row;
  AdwActionRow *display_event_types_row;
  AdwActionRow *flag_options_row;
  AdwActionRow *restart_row;

  AdwEntryRow *vcp_code_entry;
  AdwEntryRow *vcp_flags_entry;
  AdwEntryRow *multiple_vcp_codes_entry;
  AdwEntryRow *set_vcp_value_entry;
  AdwEntryRow *set_vcp_flags_entry;
  AdwEntryRow *set_vcp_context_entry;
  AdwEntryRow *sleep_multiplier_entry;
  AdwEntryRow *restart_options_entry;

  AdwSwitchRow *dynamic_sleep_row;
  AdwSwitchRow *info_logging_row;
  AdwSwitchRow *connectivity_signals_row;

  AdwSpinRow *output_level_row;
  AdwSpinRow *poll_interval_row;
  AdwSpinRow *poll_cascade_row;
  AdwSpinRow *restart_syslog_row;
  AdwSpinRow *restart_flags_row;

  GtkTextView *capabilities_text_view;
  GtkWidget *view_stack;
};

G_DEFINE_FINAL_TYPE(GnomeDdcWindow, gnomeddc_window, ADW_TYPE_APPLICATION_WINDOW)


static void gnomeddc_window_start_operation(GnomeDdcWindow *self);
static void gnomeddc_window_finish_operation(GnomeDdcWindow *self);
static void gnomeddc_window_refresh_displays(GnomeDdcWindow *self, gboolean detect);
static void gnomeddc_window_update_selection(GnomeDdcWindow *self);
static void gnomeddc_window_refresh_service_properties(GnomeDdcWindow *self);
static void gnomeddc_window_query_state(GnomeDdcWindow *self);
static void gnomeddc_window_query_sleep_multiplier(GnomeDdcWindow *self);

static gboolean
display_filter_func(gpointer item, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  if (self->search_text == NULL || *self->search_text == '\0') {
    return TRUE;
  }

  GnomeDdcDisplay *display = GNOMEDDC_DISPLAY(item);
  g_autofree gchar *folded_search = g_utf8_casefold(self->search_text, -1);
  g_autofree gchar *folded_manufacturer = g_utf8_casefold(gnomeddc_display_get_manufacturer(display), -1);
  g_autofree gchar *folded_model = g_utf8_casefold(gnomeddc_display_get_model(display), -1);
  g_autofree gchar *folded_serial = g_utf8_casefold(gnomeddc_display_get_serial(display), -1);
  g_autofree gchar *folded_edid = g_utf8_casefold(gnomeddc_display_get_edid(display), -1);

  if (strstr(folded_manufacturer, folded_search) != NULL ||
      strstr(folded_model, folded_search) != NULL ||
      strstr(folded_serial, folded_search) != NULL ||
      strstr(folded_edid, folded_search) != NULL) {
    return TRUE;
  }

  g_autofree gchar *full_name = gnomeddc_display_dup_full_name(display);
  g_autofree gchar *folded_full_name = g_utf8_casefold(full_name, -1);
  return strstr(folded_full_name, folded_search) != NULL;
}

static void
show_toast(GnomeDdcWindow *self, const gchar *format, ...)
{
  g_return_if_fail(GNOMEDDC_IS_WINDOW(self));

  va_list args;
  va_start(args, format);
  g_autofree gchar *message = g_strdup_vprintf(format, args);
  va_end(args);

  AdwToast *toast = adw_toast_new(message);
  adw_toast_overlay_add_toast(self->toast_overlay, toast);
}

static void
set_busy(GnomeDdcWindow *self, gboolean busy)
{
  if (busy) {
    gtk_spinner_start(self->busy_spinner);
    gtk_widget_set_visible(GTK_WIDGET(self->busy_spinner), TRUE);
  } else if (self->pending_calls == 0) {
    gtk_spinner_stop(self->busy_spinner);
    gtk_widget_set_visible(GTK_WIDGET(self->busy_spinner), FALSE);
  }
}

static void
gnomeddc_window_start_operation(GnomeDdcWindow *self)
{
  if (self->pending_calls == 0) {
    set_busy(self, TRUE);
  }
  self->pending_calls++;
}

static void
gnomeddc_window_finish_operation(GnomeDdcWindow *self)
{
  if (self->pending_calls > 0) {
    self->pending_calls--;
  }
  if (self->pending_calls == 0) {
    set_busy(self, FALSE);
  }
}

static void
update_empty_state(GnomeDdcWindow *self)
{
  gboolean has_items = g_list_model_get_n_items(G_LIST_MODEL(self->display_store)) > 0;
  gtk_widget_set_visible(GTK_WIDGET(self->empty_status), !has_items);
  gtk_widget_set_sensitive(self->view_stack, has_items);
}

static GnomeDdcDisplay *
get_selected_display(GnomeDdcWindow *self)
{
  guint position = gtk_single_selection_get_selected(self->selection);
  if (position == GTK_INVALID_LIST_POSITION) {
    return NULL;
  }

  return g_list_model_get_item(G_LIST_MODEL(self->selection), position);
}

static void
update_overview_rows(GnomeDdcWindow *self, GnomeDdcDisplay *display)
{
  if (display == NULL) {
    adw_action_row_set_subtitle(self->name_row, _("No display selected"));
    adw_action_row_set_subtitle(self->model_row, "");
    adw_action_row_set_subtitle(self->manufacturer_row, "");
    adw_action_row_set_subtitle(self->serial_row, "");
    adw_action_row_set_subtitle(self->product_code_row, "");
    adw_action_row_set_subtitle(self->display_number_row, "");
    adw_action_row_set_subtitle(self->usb_row, "");
    adw_action_row_set_subtitle(self->state_row, "");
    adw_action_row_set_subtitle(self->sleep_multiplier_row, "");
    return;
  }

  g_autofree gchar *full_name = gnomeddc_display_dup_full_name(display);
  adw_action_row_set_subtitle(self->name_row, full_name);
  adw_action_row_set_subtitle(self->model_row, gnomeddc_display_get_model(display));
  adw_action_row_set_subtitle(self->manufacturer_row, gnomeddc_display_get_manufacturer(display));
  adw_action_row_set_subtitle(self->serial_row, gnomeddc_display_get_serial(display));
  adw_action_row_set_subtitle(self->product_code_row,
                              g_strdup_printf("0x%04X", gnomeddc_display_get_product_code(display)));
  adw_action_row_set_subtitle(self->display_number_row,
                              g_strdup_printf("%d", gnomeddc_display_get_display_number(display)));
  adw_action_row_set_subtitle(self->usb_row,
                              g_strdup_printf("Bus %d • Device %d",
                                               gnomeddc_display_get_usb_bus(display),
                                               gnomeddc_display_get_usb_device(display)));
  adw_action_row_set_subtitle(self->state_row, _("Press Refresh to query"));
  adw_action_row_set_subtitle(self->sleep_multiplier_row, _("Press Refresh to query"));
}


static void
update_service_rows_from_dict(GnomeDdcWindow *self, GVariant *dict)
{
  self->updating_service_properties = TRUE;

  GVariantIter iter;
  const gchar *key;
  GVariant *value;
  g_variant_iter_init(&iter, dict);
  while (g_variant_iter_loop(&iter, "{sv}", &key, &value)) {
    if (g_strcmp0(key, "ServiceInterfaceVersion") == 0) {
      adw_action_row_set_subtitle(self->service_version_row, g_variant_get_string(value, NULL));
    } else if (g_strcmp0(key, "DdcutilVersion") == 0) {
      adw_action_row_set_subtitle(self->ddcutil_version_row, g_variant_get_string(value, NULL));
    } else if (g_strcmp0(key, "ServiceParametersLocked") == 0) {
      adw_action_row_set_subtitle(self->service_parameters_locked_row,
                                  g_variant_get_boolean(value) ? _("Yes") : _("No"));
    } else if (g_strcmp0(key, "AttributesReturnedByDetect") == 0) {
      GVariantIter array_iter;
      const gchar *attr;
      g_autoptr(GString) str = g_string_new(NULL);
      g_variant_iter_init(&array_iter, value);
      gboolean first = TRUE;
      while (g_variant_iter_loop(&array_iter, "s", &attr)) {
        if (!first) {
          g_string_append(str, ", ");
        }
        first = FALSE;
        g_string_append(str, attr);
      }
      adw_action_row_set_subtitle(self->attributes_row, str->str);
    } else if (g_strcmp0(key, "StatusValues") == 0) {
      GVariantIter status_iter;
      gint code;
      const gchar *text_value;
      g_autoptr(GString) str = g_string_new(NULL);
      gboolean first = TRUE;
      g_variant_iter_init(&status_iter, value);
      while (g_variant_iter_loop(&status_iter, "{is}", &code, &text_value)) {
        if (!first) {
          g_string_append_c(str, '\n');
        }
        first = FALSE;
        g_string_append_printf(str, "%d: %s", code, text_value);
      }
      adw_action_row_set_subtitle(self->status_values_row, str->str);
    } else if (g_strcmp0(key, "DisplayEventTypes") == 0) {
      GVariantIter event_iter;
      gint event_code;
      const gchar *event_text;
      g_autoptr(GString) str = g_string_new(NULL);
      gboolean first = TRUE;
      g_variant_iter_init(&event_iter, value);
      while (g_variant_iter_loop(&event_iter, "{is}", &event_code, &event_text)) {
        if (!first) {
          g_string_append_c(str, '\n');
        }
        first = FALSE;
        g_string_append_printf(str, "%d: %s", event_code, event_text);
      }
      adw_action_row_set_subtitle(self->display_event_types_row, str->str);
    } else if (g_strcmp0(key, "ServiceFlagOptions") == 0) {
      GVariantIter flag_iter;
      gint flag;
      const gchar *flag_name;
      g_autoptr(GString) str = g_string_new(NULL);
      gboolean first = TRUE;
      g_variant_iter_init(&flag_iter, value);
      while (g_variant_iter_loop(&flag_iter, "{is}", &flag, &flag_name)) {
        if (!first) {
          g_string_append_c(str, '\n');
        }
        first = FALSE;
        g_string_append_printf(str, "0x%X: %s", flag, flag_name);
      }
      adw_action_row_set_subtitle(self->flag_options_row, str->str);
    } else if (g_strcmp0(key, "DdcutilDynamicSleep") == 0) {
      adw_switch_row_set_active(self->dynamic_sleep_row, g_variant_get_boolean(value));
    } else if (g_strcmp0(key, "ServiceInfoLogging") == 0) {
      adw_switch_row_set_active(self->info_logging_row, g_variant_get_boolean(value));
    } else if (g_strcmp0(key, "ServiceEmitConnectivitySignals") == 0) {
      adw_switch_row_set_active(self->connectivity_signals_row, g_variant_get_boolean(value));
    } else if (g_strcmp0(key, "DdcutilOutputLevel") == 0) {
      adw_spin_row_set_value(self->output_level_row, g_variant_get_uint32(value));
    } else if (g_strcmp0(key, "ServicePollInterval") == 0) {
      adw_spin_row_set_value(self->poll_interval_row, g_variant_get_uint32(value));
    } else if (g_strcmp0(key, "ServicePollCascadeInterval") == 0) {
      adw_spin_row_set_value(self->poll_cascade_row, g_variant_get_double(value));
    }
  }

  self->updating_service_properties = FALSE;
}

static void
handle_get_all_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to read service properties: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  GVariant *dict = NULL;
  g_variant_get(response, "(@a{sv})", &dict);
  update_service_rows_from_dict(self, dict);
  g_variant_unref(dict);
}

static void
gnomeddc_window_refresh_service_properties(GnomeDdcWindow *self)
{
  if (!gnomeddc_client_is_connected(self->client)) {
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "org.freedesktop.DBus.Properties.GetAll",
                             g_variant_new("(s)", DDCUTIL_INTERFACE_NAME),
                             NULL,
                             handle_get_all_finished,
                             self);
}

static void
clear_capabilities_view(GnomeDdcWindow *self)
{
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(self->capabilities_text_view);
  gtk_text_buffer_set_text(buffer, "", -1);
}

static void
handle_list_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Detection failed: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  gint ddc_status = 0;
  g_autofree gchar *message = NULL;
  GVariant *array = NULL;
  gint reported_count = 0;
  g_variant_get(response, "(ia(iiisssqsu)is)", &reported_count, &array, &ddc_status, &message);

  g_list_store_remove_all(self->display_store);

  GVariantIter iter;
  gint display_number;
  gint usb_bus;
  gint usb_device;
  guint16 product_code;
  guint32 binary_serial;
  const gchar *manufacturer;
  const gchar *model;
  const gchar *serial;
  const gchar *edid;

  g_variant_iter_init(&iter, array);
  while (g_variant_iter_loop(&iter, "(iiisssqsu)",
                             &display_number,
                             &usb_bus,
                             &usb_device,
                             &manufacturer,
                             &model,
                             &serial,
                             &product_code,
                             &edid,
                             &binary_serial)) {
    GnomeDdcDisplay *display = gnomeddc_display_new(display_number,
                                                   usb_bus,
                                                   usb_device,
                                                   manufacturer,
                                                   model,
                                                   serial,
                                                   product_code,
                                                   edid,
                                                   binary_serial);
    g_list_store_append(self->display_store, display);
    g_object_unref(display);
  }
  g_variant_unref(array);

  update_empty_state(self);
  gnomeddc_window_update_selection(self);

  show_toast(self, _("Detected %u displays (%s)"),
             g_list_model_get_n_items(G_LIST_MODEL(self->display_store)),
             message != NULL ? message : "");
}

static void
gnomeddc_window_refresh_displays(GnomeDdcWindow *self, gboolean detect)
{
  if (!gnomeddc_client_is_connected(self->client)) {
    const gchar *err = gnomeddc_client_get_last_error(self->client);
    show_toast(self, "%s", err != NULL ? err : _("Unable to reach ddcutil-service"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             detect ? "Detect" : "ListDetected",
                             g_variant_new("(u)", 0),
                             NULL,
                             handle_list_finished,
                             self);
}

static gboolean
parse_uint_from_entry(AdwEntryRow *entry, guint *out_value)
{
  const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));
  if (text == NULL || *text == '\0') {
    return FALSE;
  }
  gchar *endptr = NULL;
  guint value = (guint) g_ascii_strtoull(text, &endptr, 0);
  if (endptr == NULL || *endptr != '\0') {
    return FALSE;
  }
  *out_value = value;
  return TRUE;
}

static gboolean
parse_uint16_from_entry(AdwEntryRow *entry, guint16 *out_value)
{
  guint value = 0;
  if (!parse_uint_from_entry(entry, &value)) {
    return FALSE;
  }
  if (value > G_MAXUINT16) {
    return FALSE;
  }
  *out_value = (guint16) value;
  return TRUE;
}

static gboolean
parse_uint8_from_entry(AdwEntryRow *entry, guint8 *out_value)
{
  guint value = 0;
  if (!parse_uint_from_entry(entry, &value)) {
    return FALSE;
  }
  if (value > G_MAXUINT8) {
    return FALSE;
  }
  *out_value = (guint8) value;
  return TRUE;
}

static gboolean
parse_double_from_entry(AdwEntryRow *entry, gdouble *out_value)
{
  const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));
  if (text == NULL || *text == '\0') {
    return FALSE;
  }
  gchar *endptr = NULL;
  gdouble value = g_ascii_strtod(text, &endptr);
  if (endptr == NULL || *endptr != '\0') {
    return FALSE;
  }
  *out_value = value;
  return TRUE;
}

static GVariant *
build_vcp_code_array(const gchar *text)
{
  if (text == NULL || *text == '\0') {
    return g_variant_new_array(G_VARIANT_TYPE_BYTE, NULL, 0);
  }

  g_autoptr(GPtrArray) codes = g_ptr_array_new();
  gchar **parts = g_strsplit_set(text, ",; ", -1);
  for (gint i = 0; parts[i] != NULL; i++) {
    if (parts[i][0] == '\0') {
      continue;
    }
    gchar *endptr = NULL;
    guint value = (guint) g_ascii_strtoull(parts[i], &endptr, 0);
    if (endptr == NULL || *endptr != '\0' || value > G_MAXUINT8) {
      g_strfreev(parts);
      return NULL;
    }
    g_ptr_array_add(codes, GINT_TO_POINTER((gint) value));
  }
  g_strfreev(parts);

  GVariantBuilder builder;
  g_variant_builder_init(&builder, G_VARIANT_TYPE("ay"));
  for (guint i = 0; i < codes->len; i++) {
    guint8 code = (guint8) GPOINTER_TO_INT(g_ptr_array_index(codes, i));
    g_variant_builder_add(&builder, "y", code);
  }
  return g_variant_builder_end(&builder);
}


static void
handle_get_state_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to get display state: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  gint status = 0;
  g_autofree gchar *message = NULL;
  g_variant_get(response, "(is)", &status, &message);
  adw_action_row_set_subtitle(self->state_row,
                              g_strdup_printf("%d — %s", status, message != NULL ? message : ""));
}

static void
gnomeddc_window_query_state(GnomeDdcWindow *self)
{
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "GetDisplayState",
                             g_variant_new("(isu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           0),
                             NULL,
                             handle_get_state_finished,
                             self);
}

static void
handle_get_sleep_multiplier_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to read sleep multiplier: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  gdouble multiplier = 0.0;
  gint status = 0;
  g_autofree gchar *message = NULL;
  g_variant_get(response, "(dis)", &multiplier, &status, &message);

  adw_action_row_set_subtitle(self->sleep_multiplier_row,
                              g_strdup_printf("%.3f (status %d)", multiplier, status));
  g_autofree gchar *text = g_strdup_printf("%.3f", multiplier);
  gtk_editable_set_text(GTK_EDITABLE(self->sleep_multiplier_entry), text);
}

static void
gnomeddc_window_query_sleep_multiplier(GnomeDdcWindow *self)
{
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "GetSleepMultiplier",
                             g_variant_new("(isu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           0),
                             NULL,
                             handle_get_sleep_multiplier_finished,
                             self);
}

static void
handle_set_sleep_multiplier_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to set sleep multiplier: %s"), error->message);
    return;
  }

  gint status = 0;
  g_autofree gchar *message = NULL;
  if (response != NULL) {
    g_variant_get(response, "(is)", &status, &message);
  }
  show_toast(self, _("Set sleep multiplier: %s"), message != NULL ? message : "");
  gnomeddc_window_query_sleep_multiplier(self);
}

static void
handle_get_vcp_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to read VCP: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  guint16 current = 0;
  guint16 max_value = 0;
  g_autofree gchar *formatted = NULL;
  gint status = 0;
  g_autofree gchar *message = NULL;
  g_variant_get(response, "(qqsis)", &current, &max_value, &formatted, &status, &message);
  adw_action_row_set_subtitle(self->get_vcp_row,
                              g_strdup_printf(_("Value %u / %u (status %d) — %s"),
                                              current, max_value, status,
                                              formatted != NULL ? formatted : ""));
}


static void
handle_get_multiple_vcp_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to read multiple VCP values: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  GVariant *array = NULL;
  gint status = 0;
  g_autofree gchar *message = NULL;
  g_variant_get(response, "(@a(yqqs) is)", &array, &status, &message);
  adw_action_row_set_subtitle(self->get_multiple_vcp_row,
                              g_strdup_printf(_("Status %d — %s"), status,
                                              message != NULL ? message : ""));

  GtkTextBuffer *buffer = gtk_text_view_get_buffer(self->capabilities_text_view);
  gtk_text_buffer_set_text(buffer, "", -1);

  GVariantIter iter;
  guint8 code;
  guint16 current;
  guint16 max_value;
  const gchar *formatted;
  g_variant_iter_init(&iter, array);
  while (g_variant_iter_loop(&iter, "(yqqs)", &code, &current, &max_value, &formatted)) {
    g_autofree gchar *line = g_strdup_printf("0x%02X — %u/%u — %s\n",
                                             code,
                                             current,
                                             max_value,
                                             formatted != NULL ? formatted : "");
    gtk_text_buffer_insert_at_cursor(buffer, line, -1);
  }

  g_variant_unref(array);
}

static void
handle_set_vcp_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to set VCP: %s"), error->message);
    return;
  }

  gint status = 0;
  g_autofree gchar *message = NULL;
  if (response != NULL) {
    g_variant_get(response, "(is)", &status, &message);
  }
  show_toast(self, _("Set VCP: %s"), message != NULL ? message : "");
}

static void
handle_get_vcp_metadata_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to read VCP metadata: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  const gchar *name;
  const gchar *description;
  gboolean is_read_only;
  gboolean is_write_only;
  gboolean is_rw;
  gboolean is_complex;
  gboolean is_continuous;
  gint status = 0;
  const gchar *message;
  g_variant_get(response, "(&s&sbbbbbis)",
                &name,
                &description,
                &is_read_only,
                &is_write_only,
                &is_rw,
                &is_complex,
                &is_continuous,
                &status,
                &message);

  g_autofree gchar *subtitle = g_strdup_printf(_("%s — %s (status %d)"),
                                               name,
                                               message,
                                               status);
  adw_action_row_set_subtitle(self->get_vcp_metadata_row, subtitle);

  GtkTextBuffer *buffer = gtk_text_view_get_buffer(self->capabilities_text_view);
  gtk_text_buffer_set_text(buffer, "", -1);
  g_autofree gchar *details = g_strdup_printf("Description: %s\n"
                                              "Read only: %s\n"
                                              "Write only: %s\n"
                                              "Read/Write: %s\n"
                                              "Complex: %s\n"
                                              "Continuous: %s\n",
                                              description,
                                              is_read_only ? "yes" : "no",
                                              is_write_only ? "yes" : "no",
                                              is_rw ? "yes" : "no",
                                              is_complex ? "yes" : "no",
                                              is_continuous ? "yes" : "no");
  gtk_text_buffer_insert_at_cursor(buffer, details, -1);
}

static void
handle_get_capabilities_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to read capabilities string: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  const gchar *caps_text;
  gint status = 0;
  const gchar *message;
  g_variant_get(response, "(&sis)", &caps_text, &status, &message);

  adw_action_row_set_subtitle(self->get_capabilities_row,
                              g_strdup_printf(_("Status %d — %s"), status, message));
  GtkTextBuffer *buffer = gtk_text_view_get_buffer(self->capabilities_text_view);
  gtk_text_buffer_set_text(buffer, caps_text, -1);
}

static void
handle_get_capabilities_metadata_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to read parsed capabilities: %s"), error->message);
    return;
  }

  if (response == NULL) {
    return;
  }

  const gchar *model_name;
  guint8 mccs_major = 0;
  guint8 mccs_minor = 0;
  GVariant *commands = NULL;
  GVariant *features = NULL;
  gint status = 0;
  const gchar *message;
  g_variant_get(response, "(&syy@a{ys}@a{y(ssa{ys})}is)",
                &model_name,
                &mccs_major,
                &mccs_minor,
                &commands,
                &features,
                &status,
                &message);

  adw_action_row_set_subtitle(self->get_capabilities_metadata_row,
                              g_strdup_printf(_("%s — MCCS %u.%u (status %d)"),
                                              model_name,
                                              mccs_major,
                                              mccs_minor,
                                              status));

  GtkTextBuffer *buffer = gtk_text_view_get_buffer(self->capabilities_text_view);
  gtk_text_buffer_set_text(buffer, "", -1);

  g_autofree gchar *header = g_strdup_printf("Model: %s\n"
                                             "MCCS: %u.%u\n"
                                             "Status: %d (%s)\n"
                                             "\n"
                                             "Commands:\n",
                                             model_name,
                                             mccs_major,
                                             mccs_minor,
                                             status,
                                             message);
  gtk_text_buffer_insert_at_cursor(buffer, header, -1);

  GVariantIter cmd_iter;
  guint8 cmd_code;
  const gchar *cmd_desc;
  g_variant_iter_init(&cmd_iter, commands);
  while (g_variant_iter_loop(&cmd_iter, "{ys}", &cmd_code, &cmd_desc)) {
    g_autofree gchar *line = g_strdup_printf("  0x%02X — %s\n", cmd_code, cmd_desc);
    gtk_text_buffer_insert_at_cursor(buffer, line, -1);
  }

  gtk_text_buffer_insert_at_cursor(buffer, "\nFeatures:\n", -1);
  GVariantIter feature_iter;
  guint8 feature_code;
  const gchar *feature_name;
  const gchar *feature_desc;
  GVariant *permitted = NULL;
  g_variant_iter_init(&feature_iter, features);
  while (g_variant_iter_loop(&feature_iter, "{y(ssa{ys})}",
                             &feature_code,
                             &feature_name,
                             &feature_desc,
                             &permitted)) {
    g_autofree gchar *line = g_strdup_printf("  0x%02X — %s (%s)\n",
                                             feature_code,
                                             feature_name,
                                             feature_desc);
    gtk_text_buffer_insert_at_cursor(buffer, line, -1);
    GVariantIter value_iter;
    guint8 value_code;
    const gchar *value_name;
    g_variant_iter_init(&value_iter, permitted);
    while (g_variant_iter_loop(&value_iter, "{ys}", &value_code, &value_name)) {
      g_autofree gchar *value_line = g_strdup_printf("    %u — %s\n",
                                                     value_code,
                                                     value_name);
      gtk_text_buffer_insert_at_cursor(buffer, value_line, -1);
    }
  }

  g_variant_unref(commands);
  g_variant_unref(features);
}

static void
handle_restart_finished(GObject *source G_GNUC_UNUSED, GAsyncResult *result, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GError) error = NULL;
  g_autoptr(GVariant) response = gnomeddc_client_call_finish(self->client, result, &error);
  gnomeddc_window_finish_operation(self);

  if (error != NULL) {
    show_toast(self, _("Failed to restart service: %s"), error->message);
    return;
  }

  gint status = 0;
  g_autofree gchar *message = NULL;
  if (response != NULL) {
    g_variant_get(response, "(is)", &status, &message);
  }
  show_toast(self, _("Restarted service (%s)"), message != NULL ? message : "");
}


static void
selection_changed_cb(GtkSelectionModel *model G_GNUC_UNUSED, guint position G_GNUC_UNUSED, guint n_items G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  gnomeddc_window_update_selection(self);
}

static void
search_changed_cb(GtkSearchEntry *entry, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_clear_pointer(&self->search_text, g_free);
  self->search_text = g_strdup(gtk_editable_get_text(GTK_EDITABLE(entry)));
  gtk_filter_changed(GTK_FILTER(self->search_filter), GTK_FILTER_CHANGE_DIFFERENT);
}

static void
refresh_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  gnomeddc_window_refresh_displays(self, TRUE);
}

static void
list_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  gnomeddc_window_refresh_displays(self, FALSE);
}

static void
state_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  gnomeddc_window_query_state(self);
}

static void
sleep_refresh_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  gnomeddc_window_query_sleep_multiplier(self);
}

static void
sleep_set_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }

  gdouble multiplier = 0.0;
  if (!parse_double_from_entry(self->sleep_multiplier_entry, &multiplier)) {
    show_toast(self, _("Enter a valid multiplier"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "SetSleepMultiplier",
                             g_variant_new("(isdu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           multiplier,
                                           0),
                             NULL,
                             handle_set_sleep_multiplier_finished,
                             self);
}

static void
get_vcp_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }

  guint8 vcp_code = 0;
  guint flags = 0;
  if (!parse_uint8_from_entry(self->vcp_code_entry, &vcp_code) ||
      !parse_uint_from_entry(self->vcp_flags_entry, &flags)) {
    show_toast(self, _("Enter a valid VCP code and flags"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "GetVcp",
                             g_variant_new("(isyu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           vcp_code,
                                           flags),
                             NULL,
                             handle_get_vcp_finished,
                             self);
}

static void
get_multiple_vcp_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }

  g_autoptr(GVariant) codes = build_vcp_code_array(gtk_editable_get_text(GTK_EDITABLE(self->multiple_vcp_codes_entry)));
  if (codes == NULL) {
    show_toast(self, _("Enter valid VCP codes"));
    return;
  }
  guint flags = 0;
  if (!parse_uint_from_entry(self->vcp_flags_entry, &flags)) {
    show_toast(self, _("Enter valid flags"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "GetMultipleVcp",
                             g_variant_new("(isayu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           codes,
                                           flags),
                             NULL,
                             handle_get_multiple_vcp_finished,
                             self);
}

static void
set_vcp_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }

  guint8 vcp_code = 0;
  guint16 value = 0;
  guint flags = 0;
  if (!parse_uint8_from_entry(self->vcp_code_entry, &vcp_code) ||
      !parse_uint16_from_entry(self->set_vcp_value_entry, &value) ||
      !parse_uint_from_entry(self->set_vcp_flags_entry, &flags)) {
    show_toast(self, _("Enter valid code, value, and flags"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "SetVcp",
                             g_variant_new("(isyqu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           vcp_code,
                                           value,
                                           flags),
                             NULL,
                             handle_set_vcp_finished,
                             self);
}

static void
set_vcp_context_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }

  guint8 vcp_code = 0;
  guint16 value = 0;
  guint flags = 0;
  const gchar *context = gtk_editable_get_text(GTK_EDITABLE(self->set_vcp_context_entry));
  if (!parse_uint8_from_entry(self->vcp_code_entry, &vcp_code) ||
      !parse_uint16_from_entry(self->set_vcp_value_entry, &value) ||
      !parse_uint_from_entry(self->set_vcp_flags_entry, &flags)) {
    show_toast(self, _("Enter valid code, value, and flags"));
    return;
  }

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "SetVcpWithContext",
                             g_variant_new("(isyqsu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           vcp_code,
                                           value,
                                           context != NULL ? context : "",
                                           flags),
                             NULL,
                             handle_set_vcp_finished,
                             self);
}

static void
get_vcp_metadata_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }
  guint8 vcp_code = 0;
  guint flags = 0;
  if (!parse_uint8_from_entry(self->vcp_code_entry, &vcp_code) ||
      !parse_uint_from_entry(self->vcp_flags_entry, &flags)) {
    show_toast(self, _("Enter valid code and flags"));
    return;
  }
  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "GetVcpMetadata",
                             g_variant_new("(isyu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           vcp_code,
                                           flags),
                             NULL,
                             handle_get_vcp_metadata_finished,
                             self);
}

static void
get_capabilities_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }
  guint flags = 0;
  if (!parse_uint_from_entry(self->vcp_flags_entry, &flags)) {
    show_toast(self, _("Enter valid flags"));
    return;
  }
  clear_capabilities_view(self);
  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "GetCapabilitiesString",
                             g_variant_new("(isu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           flags),
                             NULL,
                             handle_get_capabilities_finished,
                             self);
}

static void
get_capabilities_metadata_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  if (display == NULL) {
    show_toast(self, _("Select a display first"));
    return;
  }
  guint flags = 0;
  if (!parse_uint_from_entry(self->vcp_flags_entry, &flags)) {
    show_toast(self, _("Enter valid flags"));
    return;
  }
  clear_capabilities_view(self);
  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "GetCapabilitiesMetadata",
                             g_variant_new("(isu)",
                                           gnomeddc_display_get_display_number(display),
                                           gnomeddc_display_get_edid(display),
                                           flags),
                             NULL,
                             handle_get_capabilities_metadata_finished,
                             self);
}

static void
restart_clicked_cb(GtkButton *button G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  const gchar *options = gtk_editable_get_text(GTK_EDITABLE(self->restart_options_entry));
  guint syslog_level = (guint) adw_spin_row_get_value(self->restart_syslog_row);
  guint flags = (guint) adw_spin_row_get_value(self->restart_flags_row);

  gnomeddc_window_start_operation(self);
  gnomeddc_client_call_async(self->client,
                             "Restart",
                             g_variant_new("(suu)", options != NULL ? options : "", syslog_level, flags),
                             NULL,
                             handle_restart_finished,
                             self);
}

static void
service_switch_toggled_cb(AdwSwitchRow *row, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  if (self->updating_service_properties) {
    return;
  }

  const gchar *property_name = NULL;
  if (row == self->dynamic_sleep_row) {
    property_name = "DdcutilDynamicSleep";
  } else if (row == self->info_logging_row) {
    property_name = "ServiceInfoLogging";
  } else if (row == self->connectivity_signals_row) {
    property_name = "ServiceEmitConnectivitySignals";
  }

  if (property_name == NULL) {
    return;
  }

  gnomeddc_client_set_property_async(self->client,
                                     property_name,
                                     g_variant_new_boolean(adw_switch_row_get_active(row)),
                                     NULL,
                                     NULL,
                                     NULL);
}

static void
spin_row_value_changed_cb(GObject *object, GParamSpec *pspec G_GNUC_UNUSED, gpointer user_data)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(user_data);
  if (self->updating_service_properties) {
    return;
  }

  const gchar *property_name = NULL;
  gdouble value = 0.0;
  if (object == G_OBJECT(self->output_level_row)) {
    property_name = "DdcutilOutputLevel";
    value = adw_spin_row_get_value(self->output_level_row);
    gnomeddc_client_set_property_async(self->client,
                                       property_name,
                                       g_variant_new_uint32((guint32) value),
                                       NULL,
                                       NULL,
                                       NULL);
    return;
  }
  if (object == G_OBJECT(self->poll_interval_row)) {
    property_name = "ServicePollInterval";
    value = adw_spin_row_get_value(self->poll_interval_row);
    gnomeddc_client_set_property_async(self->client,
                                       property_name,
                                       g_variant_new_uint32((guint32) value),
                                       NULL,
                                       NULL,
                                       NULL);
    return;
  }
  if (object == G_OBJECT(self->poll_cascade_row)) {
    property_name = "ServicePollCascadeInterval";
    value = adw_spin_row_get_value(self->poll_cascade_row);
    gnomeddc_client_set_property_async(self->client,
                                       property_name,
                                       g_variant_new_double(value),
                                       NULL,
                                       NULL,
                                       NULL);
  }
}


static void
gnomeddc_window_update_selection(GnomeDdcWindow *self)
{
  g_autoptr(GnomeDdcDisplay) display = get_selected_display(self);
  update_overview_rows(self, display);
  if (display == NULL) {
    clear_capabilities_view(self);
    adw_action_row_set_subtitle(self->get_vcp_row, "");
    adw_action_row_set_subtitle(self->get_multiple_vcp_row, "");
    adw_action_row_set_subtitle(self->get_vcp_metadata_row, "");
    adw_action_row_set_subtitle(self->get_capabilities_row, "");
    adw_action_row_set_subtitle(self->get_capabilities_metadata_row, "");
    return;
  }
}


static void
display_list_setup(GtkSignalListItemFactory *factory G_GNUC_UNUSED, GtkListItem *list_item, gpointer user_data G_GNUC_UNUSED)
{
  AdwActionRow *row = g_object_new(ADW_TYPE_ACTION_ROW,
                                   "activatable", FALSE,
                                   NULL);
  gtk_list_item_set_child(list_item, GTK_WIDGET(row));
}

static void
display_list_bind(GtkSignalListItemFactory *factory G_GNUC_UNUSED, GtkListItem *list_item, gpointer user_data G_GNUC_UNUSED)
{
  GnomeDdcDisplay *display = GNOMEDDC_DISPLAY(gtk_list_item_get_item(list_item));
  AdwActionRow *row = ADW_ACTION_ROW(gtk_list_item_get_child(list_item));
  g_autofree gchar *full_name = gnomeddc_display_dup_full_name(display);
  g_autofree gchar *subtitle = g_strdup_printf(_("Display %d — %s"),
                                               gnomeddc_display_get_display_number(display),
                                               gnomeddc_display_get_serial(display));
  adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), full_name);
  adw_action_row_set_subtitle(row, subtitle);
}


static void
gnomeddc_window_dispose(GObject *object)
{
  GnomeDdcWindow *self = GNOMEDDC_WINDOW(object);
  g_clear_object(&self->client);
  g_clear_object(&self->display_store);
  g_clear_object(&self->filter_model);
  g_clear_object(&self->search_filter);
  g_clear_object(&self->selection);
  g_clear_pointer(&self->search_text, g_free);
  G_OBJECT_CLASS(gnomeddc_window_parent_class)->dispose(object);
}

static void
gnomeddc_window_class_init(GnomeDdcWindowClass *klass)
{
  GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(klass);
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = gnomeddc_window_dispose;

  gtk_widget_class_set_template_from_resource(widget_class,
                                              "/com/ddcutil/GnomeDDC/ui/gnomeddc-window.ui");

  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, toast_overlay);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, display_list);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, display_search_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, empty_status);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, busy_spinner);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, refresh_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, list_detected_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, detect_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, query_state_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, sleep_multiplier_refresh_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_vcp_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_multiple_vcp_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_vcp_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_vcp_context_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_vcp_metadata_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_capabilities_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_capabilities_metadata_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_sleep_multiplier_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, restart_button);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, name_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, model_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, manufacturer_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, serial_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, product_code_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, display_number_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, usb_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, state_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, sleep_multiplier_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_vcp_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_multiple_vcp_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_vcp_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_vcp_context_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_vcp_metadata_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_capabilities_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, get_capabilities_metadata_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_sleep_multiplier_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, service_version_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, ddcutil_version_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, service_parameters_locked_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, attributes_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, status_values_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, display_event_types_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, flag_options_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, restart_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, vcp_code_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, vcp_flags_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, multiple_vcp_codes_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_vcp_value_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_vcp_flags_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, set_vcp_context_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, sleep_multiplier_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, restart_options_entry);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, dynamic_sleep_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, info_logging_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, connectivity_signals_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, output_level_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, poll_interval_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, poll_cascade_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, restart_syslog_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, restart_flags_row);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, capabilities_text_view);
  gtk_widget_class_bind_template_child(widget_class, GnomeDdcWindow, view_stack);
}

static void
gnomeddc_window_init(GnomeDdcWindow *self)
{
  gtk_widget_init_template(GTK_WIDGET(self));

  self->client = gnomeddc_client_new();
  self->display_store = g_list_store_new(GNOMEDDC_TYPE_DISPLAY);
  self->search_filter = gtk_custom_filter_new(display_filter_func, self, NULL);
  self->filter_model = gtk_filter_list_model_new(G_LIST_MODEL(self->display_store), GTK_FILTER(self->search_filter));
  self->selection = gtk_single_selection_new(G_LIST_MODEL(self->filter_model));

  GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
  g_signal_connect(factory, "setup", G_CALLBACK(display_list_setup), self);
  g_signal_connect(factory, "bind", G_CALLBACK(display_list_bind), self);
  gtk_list_view_set_factory(self->display_list, factory);
  g_object_unref(factory);

  gtk_list_view_set_model(self->display_list, GTK_SELECTION_MODEL(self->selection));

  g_signal_connect(self->selection, "selection-changed", G_CALLBACK(selection_changed_cb), self);
  g_signal_connect(self->display_search_entry, "search-changed", G_CALLBACK(search_changed_cb), self);
  g_signal_connect(self->refresh_button, "clicked", G_CALLBACK(refresh_clicked_cb), self);
  g_signal_connect(self->list_detected_button, "clicked", G_CALLBACK(list_clicked_cb), self);
  g_signal_connect(self->detect_button, "clicked", G_CALLBACK(refresh_clicked_cb), self);
  g_signal_connect(self->query_state_button, "clicked", G_CALLBACK(state_clicked_cb), self);
  g_signal_connect(self->sleep_multiplier_refresh_button, "clicked", G_CALLBACK(sleep_refresh_clicked_cb), self);
  g_signal_connect(self->set_sleep_multiplier_button, "clicked", G_CALLBACK(sleep_set_clicked_cb), self);
  g_signal_connect(self->get_vcp_button, "clicked", G_CALLBACK(get_vcp_clicked_cb), self);
  g_signal_connect(self->get_multiple_vcp_button, "clicked", G_CALLBACK(get_multiple_vcp_clicked_cb), self);
  g_signal_connect(self->set_vcp_button, "clicked", G_CALLBACK(set_vcp_clicked_cb), self);
  g_signal_connect(self->set_vcp_context_button, "clicked", G_CALLBACK(set_vcp_context_clicked_cb), self);
  g_signal_connect(self->get_vcp_metadata_button, "clicked", G_CALLBACK(get_vcp_metadata_clicked_cb), self);
  g_signal_connect(self->get_capabilities_button, "clicked", G_CALLBACK(get_capabilities_clicked_cb), self);
  g_signal_connect(self->get_capabilities_metadata_button, "clicked", G_CALLBACK(get_capabilities_metadata_clicked_cb), self);
  g_signal_connect(self->restart_button, "clicked", G_CALLBACK(restart_clicked_cb), self);

  g_signal_connect(self->dynamic_sleep_row, "notify::active", G_CALLBACK(service_switch_toggled_cb), self);
  g_signal_connect(self->info_logging_row, "notify::active", G_CALLBACK(service_switch_toggled_cb), self);
  g_signal_connect(self->connectivity_signals_row, "notify::active", G_CALLBACK(service_switch_toggled_cb), self);

  g_signal_connect(self->output_level_row, "notify::value", G_CALLBACK(spin_row_value_changed_cb), self);
  g_signal_connect(self->poll_interval_row, "notify::value", G_CALLBACK(spin_row_value_changed_cb), self);
  g_signal_connect(self->poll_cascade_row, "notify::value", G_CALLBACK(spin_row_value_changed_cb), self);

  update_empty_state(self);

  if (!gnomeddc_client_is_connected(self->client)) {
    const gchar *error = gnomeddc_client_get_last_error(self->client);
    show_toast(self, "%s", error != NULL ? error : _("Unable to reach ddcutil-service"));
  } else {
    gnomeddc_window_refresh_displays(self, FALSE);
    gnomeddc_window_refresh_service_properties(self);
  }
}

