#include <adwaita.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

#include "ddcutil_client.h"
#include "monitor_item.h"

typedef struct _AppWindow AppWindow;

typedef enum {
    PICTURE_CONTROL_BRIGHTNESS = 0,
    PICTURE_CONTROL_CONTRAST,
    PICTURE_CONTROL_SHARPNESS,
    PICTURE_CONTROL_GAMMA,
    PICTURE_CONTROL_RED_GAIN,
    PICTURE_CONTROL_GREEN_GAIN,
    PICTURE_CONTROL_BLUE_GAIN,
    PICTURE_CONTROL_SATURATION,
    PICTURE_CONTROL_HUE,
    PICTURE_CONTROL_VOLUME,
    PICTURE_CONTROL_COUNT
} PictureControlType;

typedef struct {
    const gchar *title;
    guint8 vcp_code;
    const gchar *feedback_prefix;
    const gchar *group_id;
} PictureControlSpec;

static const PictureControlSpec picture_control_specs[PICTURE_CONTROL_COUNT] = {
    [PICTURE_CONTROL_BRIGHTNESS] = {"Brightness", 0x10, "Brightness", "picture"},
    [PICTURE_CONTROL_CONTRAST] = {"Contrast", 0x12, "Contrast", "picture"},
    [PICTURE_CONTROL_SHARPNESS] = {"Sharpness", 0x87, "Sharpness", "picture"},
    [PICTURE_CONTROL_GAMMA] = {"Gamma", 0x72, "Gamma", "picture"},
    [PICTURE_CONTROL_RED_GAIN] = {"Red", 0x16, "Red", "color"},
    [PICTURE_CONTROL_GREEN_GAIN] = {"Green", 0x18, "Green", "color"},
    [PICTURE_CONTROL_BLUE_GAIN] = {"Blue", 0x1A, "Blue", "color"},
    [PICTURE_CONTROL_SATURATION] = {"Saturation", 0x8A, "Saturation", "color"},
    [PICTURE_CONTROL_HUE] = {"Hue", 0x8B, "Hue", "color"},
    [PICTURE_CONTROL_VOLUME] = {"Volume", 0x62, "Volume", "audio"},
};

typedef struct {
    const gchar *id;
    const gchar *title;
} PictureControlGroupSpec;

static const PictureControlGroupSpec picture_group_specs[] = {
    {"picture", "Picture"},
    {"color", "Color balance"},
    {"audio", "Audio"},
};

typedef struct {
    AppWindow *app;
    GtkScale *scale;
    GtkLabel *value_label;
    GtkWidget *row;
    guint8 vcp_code;
    gint max;
    gint pending_value;
    gint last_sent_value;
    gboolean updating;
    gboolean dragging;
    const gchar *feedback_prefix;
} PictureControl;

typedef struct {
    gchar *display_id;
    guint n_codes;
    guint8 *codes;
    guint generation;
} PictureLoadTaskData;

typedef struct {
    gchar *display_id;
    guint n_codes;
    DdcutilVcpValue *values;
    guint generation;
} PictureLoadResult;

struct _AppWindow {
    AdwApplicationWindow *window;
    GtkButton *refresh_button;
    GtkStack *content_stack;
    AdwStatusPage *status_page;
    GtkLabel *title_label;
    GtkLabel *subtitle_label;
    GtkLabel *feedback_label;
    GtkButton *rename_button;
    AdwViewStack *section_stack;
    AdwActionRow *bus_row;
    AdwActionRow *serial_row;
    AdwActionRow *manufacturer_row;
    AdwActionRow *mccs_row;
    AdwActionRow *firmware_row;
    AdwActionRow *manufacture_row;
    GListStore *monitor_store;
    GtkListBox *monitor_list;
    guint current_position;
    PictureControl picture_controls[PICTURE_CONTROL_COUNT];
    gboolean suppress_selection_signal;
    GtkSizeGroup *slider_size_group;
    GCancellable *refresh_cancellable;
    GCancellable *load_cancellable;
    gchar *loading_display_id;
    guint load_generation;
};

static void app_window_refresh(AppWindow *self);
static void app_window_show_monitor(AppWindow *self, MonitorItem *item);
static void app_window_set_feedback(AppWindow *self, const gchar *message);
static void app_window_reset_picture_controls(AppWindow *self);
static void picture_control_clear(PictureControl *control);
static void picture_control_enable(PictureControl *control);
static void picture_control_update_label(PictureControl *control, gdouble value);
static void picture_control_apply(PictureControl *control, gint new_value);
static void picture_control_apply_loaded_value(PictureControl *control, gint current, gint maximum);
static void app_window_update_identity(AppWindow *self, MonitorItem *item);
static void app_window_apply_display_name(AppWindow *self, const gchar *entry_text);
static void app_window_cancel_refresh(AppWindow *self);
static void app_window_cancel_load(AppWindow *self);
static void app_window_start_picture_load(AppWindow *self, MonitorItem *item);
static void refresh_task_complete(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void picture_load_task_complete(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void picture_load_task_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void refresh_task_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable);
static void on_picture_drag_begin(GtkGestureDrag *gesture, gdouble start_x, gdouble start_y, gpointer user_data);
static void on_picture_drag_end(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y, gpointer user_data);
static void on_picture_drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data);
static void on_picture_value_changed(GtkRange *range, gpointer user_data);
static void on_rename_button_clicked(GtkButton *button, gpointer user_data);
static void on_rename_dialog_response(AdwMessageDialog *dialog, const gchar *response, gpointer user_data);
static void on_rename_entry_activated(GtkEntry *entry, gpointer user_data);
static void on_prefer_dark_changed(GObject *settings, GParamSpec *pspec, gpointer user_data);
static void update_color_scheme(AdwApplication *app);
static gboolean color_scheme_watched = FALSE;

static void picture_load_task_data_free(PictureLoadTaskData *data) {
    if (!data) {
        return;
    }
    g_free(data->display_id);
    g_free(data->codes);
    g_free(data);
}

static void picture_load_result_free(PictureLoadResult *result) {
    if (!result) {
        return;
    }
    if (result->values) {
        for (guint i = 0; i < result->n_codes; i++) {
            g_free(result->values[i].error_message);
        }
    }
    g_free(result->values);
    g_free(result->display_id);
    g_free(result);
}

static void app_window_cancel_refresh(AppWindow *self) {
    if (!self) {
        return;
    }
    if (self->refresh_cancellable) {
        g_cancellable_cancel(self->refresh_cancellable);
        g_clear_object(&self->refresh_cancellable);
    }
}

static void app_window_cancel_load(AppWindow *self) {
    if (!self) {
        return;
    }
    if (self->load_cancellable) {
        g_cancellable_cancel(self->load_cancellable);
        g_clear_object(&self->load_cancellable);
    }
    g_clear_pointer(&self->loading_display_id, g_free);
}

static void app_window_free(AppWindow *self) {
    if (!self) {
        return;
    }
    app_window_cancel_refresh(self);
    app_window_cancel_load(self);
    g_clear_object(&self->monitor_store);
    g_clear_object(&self->slider_size_group);
    g_free(self);
}

static void app_window_set_feedback(AppWindow *self, const gchar *message) {
    gtk_label_set_text(self->feedback_label, message ? message : "");
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    AppWindow *self = user_data;
    app_window_refresh(self);
    (void)button;
}

static void picture_control_update_label(PictureControl *control, gdouble value) {
    if (!control || !control->value_label) {
        return;
    }

    if (!gtk_widget_get_sensitive(GTK_WIDGET(control->scale))) {
        return;
    }

    gint max = control->max > 0 ? control->max : 100;
    gdouble percent_value = (max > 0) ? (value * 100.0 / (gdouble)max) : 0.0;
    if (percent_value < 0.0) {
        percent_value = 0.0;
    } else if (percent_value > 100.0) {
        percent_value = 100.0;
    }

    gint percent = (gint)(percent_value + 0.5);
    gchar *label = g_strdup_printf("%d%%", percent);
    gtk_label_set_text(control->value_label, label);
    g_free(label);
}

static void picture_control_apply_loaded_value(PictureControl *control, gint current, gint maximum) {
    if (!control || !control->scale) {
        return;
    }

    picture_control_enable(control);

    control->updating = TRUE;
    control->max = maximum > 0 ? maximum : 100;
    gtk_range_set_range(GTK_RANGE(control->scale), 0.0, (gdouble)control->max);
    gtk_range_set_value(GTK_RANGE(control->scale), current);
    control->pending_value = current;
    control->last_sent_value = current;
    control->dragging = FALSE;
    control->updating = FALSE;

    picture_control_update_label(control, current);
}

static void picture_control_clear(PictureControl *control) {
    if (!control || !control->scale || !control->value_label) {
        return;
    }

    control->updating = TRUE;
    gtk_range_set_range(GTK_RANGE(control->scale), 0, 100);
    gtk_range_set_value(GTK_RANGE(control->scale), 0);
    control->max = 100;
    control->pending_value = 0;
    control->last_sent_value = -1;
    control->dragging = FALSE;
    gtk_label_set_text(control->value_label, "—");
    control->updating = FALSE;
    if (control->row) {
        gtk_widget_set_sensitive(control->row, FALSE);
    }
    gtk_widget_set_sensitive(GTK_WIDGET(control->scale), FALSE);
}

static void picture_control_enable(PictureControl *control) {
    if (!control) {
        return;
    }
    if (control->row) {
        gtk_widget_set_sensitive(control->row, TRUE);
    }
    gtk_widget_set_sensitive(GTK_WIDGET(control->scale), TRUE);
}

static void app_window_reset_picture_controls(AppWindow *self) {
    if (!self) {
        return;
    }
    for (guint i = 0; i < PICTURE_CONTROL_COUNT; i++) {
        picture_control_clear(&self->picture_controls[i]);
    }
}

static void picture_control_apply(PictureControl *control, gint new_value) {
    if (!control || !control->app || !control->scale) {
        return;
    }

    if (!gtk_widget_get_sensitive(GTK_WIDGET(control->scale))) {
        return;
    }

    AppWindow *self = control->app;
    if (!self->monitor_store) {
        return;
    }

    guint position = self->current_position;
    if (position == GTK_INVALID_LIST_POSITION) {
        return;
    }

    g_autoptr(GObject) selected = g_list_model_get_item(G_LIST_MODEL(self->monitor_store), position);
    if (!selected) {
        return;
    }

    MonitorItem *item = MONITOR_ITEM(selected);
    control->pending_value = new_value;

    GError *error = NULL;
    if (!ddcutil_set_vcp_value(monitor_item_get_display_id(item), control->vcp_code, new_value, &error)) {
        app_window_set_feedback(self, error ? error->message : "Failed to update control");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    gint max = control->max > 0 ? control->max : 100;
    gint percent = max > 0 ? (gint)((new_value * 100.0 / (gdouble)max) + 0.5) : new_value;
    gchar *message = g_strdup_printf("%s set to %d%%", control->feedback_prefix, percent);
    app_window_set_feedback(self, message);
    g_free(message);
    control->last_sent_value = new_value;
}

static void app_window_update_identity(AppWindow *self, MonitorItem *item) {
    if (!self || !self->rename_button) {
        return;
    }

    gboolean has_item = item != NULL;
    gtk_widget_set_sensitive(GTK_WIDGET(self->rename_button), has_item);
}

static void app_window_apply_display_name(AppWindow *self, const gchar *entry_text) {
    if (!self || self->current_position == GTK_INVALID_LIST_POSITION) {
        return;
    }

    gchar *dup = g_strdup(entry_text ? entry_text : "");
    gchar *stripped = g_strstrip(dup);

    g_autoptr(GObject) selected = g_list_model_get_item(G_LIST_MODEL(self->monitor_store), self->current_position);
    if (!selected) {
        g_free(dup);
        return;
    }

    MonitorItem *item = MONITOR_ITEM(selected);
    const gchar *current_name = monitor_item_get_name(item);

    if (!stripped || *stripped == '\0') {
        app_window_set_feedback(self, "Display name cannot be empty.");
        g_free(dup);
        return;
    }

    if (g_strcmp0(current_name, stripped) == 0) {
        g_free(dup);
        return;
    }

    monitor_item_set_name(item, stripped);
    gtk_label_set_text(self->title_label, stripped);

    GtkListBoxRow *row = gtk_list_box_get_row_at_index(self->monitor_list, self->current_position);
    if (row) {
        GtkWidget *child = gtk_list_box_row_get_child(row);
        if (ADW_IS_ACTION_ROW(child)) {
            adw_preferences_row_set_title(ADW_PREFERENCES_ROW(child), stripped);
        }
    }

    gchar *message = g_strdup_printf("Display renamed to %s", stripped);
    app_window_set_feedback(self, message);
    g_free(message);

    g_free(dup);
}

static void on_picture_drag_begin(GtkGestureDrag *gesture, gdouble start_x, gdouble start_y, gpointer user_data) {
    PictureControl *control = user_data;
    if (!control) {
        return;
    }

    control->dragging = TRUE;
    control->pending_value = (gint)gtk_range_get_value(GTK_RANGE(control->scale));
    (void)gesture;
    (void)start_x;
    (void)start_y;
}

static void on_picture_drag_end(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y, gpointer user_data) {
    PictureControl *control = user_data;
    if (!control) {
        return;
    }

    control->dragging = FALSE;
    gint value = (gint)gtk_range_get_value(GTK_RANGE(control->scale));
    control->pending_value = value;
    picture_control_apply(control, value);
    (void)gesture;
    (void)offset_x;
    (void)offset_y;
}

static void on_picture_drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data) {
    PictureControl *control = user_data;
    if (!control) {
        return;
    }

    control->dragging = FALSE;
    gint value = (gint)gtk_range_get_value(GTK_RANGE(control->scale));
    control->pending_value = value;
    picture_control_apply(control, value);
    (void)gesture;
    (void)sequence;
}

static void on_picture_value_changed(GtkRange *range, gpointer user_data) {
    PictureControl *control = user_data;
    if (!control) {
        return;
    }

    gdouble value = gtk_range_get_value(range);
    picture_control_update_label(control, value);

    if (control->updating) {
        return;
    }

    gint new_value = (gint)value;

    if (!gtk_widget_get_sensitive(GTK_WIDGET(control->scale))) {
        return;
    }

    if (!control->dragging && new_value == control->last_sent_value) {
        return;
    }

    if (control->dragging) {
        control->pending_value = new_value;
        return;
    }

    picture_control_apply(control, new_value);
}

static void on_rename_button_clicked(GtkButton *button, gpointer user_data) {
    AppWindow *self = user_data;
    if (!self || self->current_position == GTK_INVALID_LIST_POSITION) {
        return;
    }

    g_autoptr(GObject) selected = g_list_model_get_item(G_LIST_MODEL(self->monitor_store), self->current_position);
    if (!selected) {
        return;
    }

    MonitorItem *item = MONITOR_ITEM(selected);
    const gchar *current_name = monitor_item_get_name(item);

    GtkWidget *dialog_widget = adw_message_dialog_new(GTK_WINDOW(self->window), "Rename Display", NULL);
    AdwMessageDialog *dialog = ADW_MESSAGE_DIALOG(dialog_widget);
    adw_message_dialog_add_response(dialog, "cancel", "_Cancel");
    adw_message_dialog_add_response(dialog, "rename", "_Rename");
    adw_message_dialog_set_default_response(dialog, "rename");
    adw_message_dialog_set_close_response(dialog, "cancel");
    adw_message_dialog_set_response_appearance(dialog, "rename", ADW_RESPONSE_SUGGESTED);

    GtkWidget *entry = gtk_entry_new();
    gtk_widget_set_hexpand(entry, TRUE);
    gtk_editable_set_text(GTK_EDITABLE(entry), current_name ? current_name : "");
    adw_message_dialog_set_extra_child(dialog, entry);

    g_object_set_data(G_OBJECT(dialog), "rename-entry", entry);
    g_signal_connect(dialog, "response", G_CALLBACK(on_rename_dialog_response), self);
    g_signal_connect(entry, "activate", G_CALLBACK(on_rename_entry_activated), dialog);

    adw_dialog_present(ADW_DIALOG(dialog), GTK_WIDGET(self->window));
    gtk_widget_grab_focus(entry);
    (void)button;
}

static void on_rename_dialog_response(AdwMessageDialog *dialog, const gchar *response, gpointer user_data) {
    AppWindow *self = user_data;
    GtkWidget *entry = GTK_WIDGET(g_object_get_data(G_OBJECT(dialog), "rename-entry"));

    if (self && g_strcmp0(response, "rename") == 0 && GTK_IS_ENTRY(entry)) {
        const gchar *text = gtk_editable_get_text(GTK_EDITABLE(entry));
        app_window_apply_display_name(self, text);
    }

    gtk_window_destroy(GTK_WINDOW(dialog));
}

static void on_rename_entry_activated(GtkEntry *entry, gpointer user_data) {
    AdwMessageDialog *dialog = ADW_MESSAGE_DIALOG(user_data);
    adw_message_dialog_response(dialog, "rename");
    (void)entry;
}

static GtkWidget *create_sidebar_row(gpointer item, gpointer user_data) {
    MonitorItem *monitor = MONITOR_ITEM(item);
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), monitor_item_get_name(monitor));

    const gchar *bus = monitor_item_get_bus(monitor);
    if (bus && *bus) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), bus);
    }

    g_object_set_data_full(G_OBJECT(row), "monitor-item", g_object_ref(monitor), g_object_unref);
    (void)user_data;

    return row;
}

static void app_window_set_content_status(AppWindow *self,
                                          const gchar *icon,
                                          const gchar *title,
                                          const gchar *description) {
    adw_status_page_set_icon_name(self->status_page, icon);
    adw_status_page_set_title(self->status_page, title);
    adw_status_page_set_description(self->status_page, description);
    gtk_stack_set_visible_child_name(self->content_stack, "status");
}

static void app_window_select_position(AppWindow *self, guint position) {
    if (!self->monitor_list) {
        return;
    }

    if (position == GTK_INVALID_LIST_POSITION) {
        self->suppress_selection_signal = TRUE;
        gtk_list_box_unselect_all(self->monitor_list);
        self->suppress_selection_signal = FALSE;
        self->current_position = GTK_INVALID_LIST_POSITION;
        app_window_cancel_load(self);
        app_window_reset_picture_controls(self);
        app_window_update_identity(self, NULL);
        app_window_set_content_status(self,
                                      "computer-symbolic",
                                      "No monitor selected",
                                      "Choose a display from the sidebar to adjust its picture settings.");
        return;
    }

    GtkListBoxRow *row = gtk_list_box_get_row_at_index(self->monitor_list, position);
    if (!row) {
        return;
    }

    self->suppress_selection_signal = TRUE;
    gtk_list_box_select_row(self->monitor_list, row);
    self->suppress_selection_signal = FALSE;

    MonitorItem *item = g_object_get_data(G_OBJECT(row), "monitor-item");
    if (!item) {
        return;
    }

    self->current_position = position;
    app_window_cancel_load(self);
    app_window_reset_picture_controls(self);
    app_window_show_monitor(self, item);
}

static void on_monitor_row_activated(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AppWindow *self = user_data;
    guint position = gtk_list_box_row_get_index(row);
    app_window_select_position(self, position);
    (void)box;
}

static void on_monitor_row_selected(GtkListBox *box, GtkListBoxRow *row, gpointer user_data) {
    AppWindow *self = user_data;
    if (self->suppress_selection_signal) {
        return;
    }

    if (!row) {
        app_window_select_position(self, GTK_INVALID_LIST_POSITION);
        return;
    }

    guint index = gtk_list_box_row_get_index(row);
    app_window_select_position(self, index);
    (void)box;
}

static GtkWidget *build_sidebar(AppWindow *self) {
    GtkWidget *toolbar_view = adw_toolbar_view_new();
    gtk_widget_add_css_class(toolbar_view, "navigation-sidebar");

    GtkWidget *header_bar = adw_header_bar_new();
    gtk_widget_add_css_class(header_bar, "flat");
    g_object_set(header_bar, "show-start-title-buttons", FALSE, "show-end-title-buttons", FALSE, NULL);

    GtkWidget *title_widget = adw_window_title_new("GnomeDDC", NULL);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_bar), title_widget);

    if (self->refresh_button) {
        gtk_widget_add_css_class(GTK_WIDGET(self->refresh_button), "flat");
        adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), GTK_WIDGET(self->refresh_button));
    }

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header_bar);

    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    gtk_widget_set_margin_top(content_box, 18);
    gtk_widget_set_margin_bottom(content_box, 24);
    gtk_widget_set_margin_start(content_box, 24);
    gtk_widget_set_margin_end(content_box, 24);

    GtkWidget *category_label = gtk_label_new("Displays");
    gtk_widget_add_css_class(category_label, "heading");
    gtk_widget_set_halign(category_label, GTK_ALIGN_START);
    gtk_label_set_xalign(GTK_LABEL(category_label), 0.0f);
    gtk_widget_set_margin_bottom(category_label, 12);
    gtk_box_append(GTK_BOX(content_box), category_label);

    GtkWidget *list_box = gtk_list_box_new();
    gtk_widget_add_css_class(list_box, "navigation-sidebar");
    gtk_list_box_set_selection_mode(GTK_LIST_BOX(list_box), GTK_SELECTION_BROWSE);
    gtk_list_box_set_activate_on_single_click(GTK_LIST_BOX(list_box), TRUE);
    gtk_list_box_bind_model(GTK_LIST_BOX(list_box), G_LIST_MODEL(self->monitor_store), create_sidebar_row, self, NULL);
    g_signal_connect(list_box, "row-activated", G_CALLBACK(on_monitor_row_activated), self);
    g_signal_connect(list_box, "row-selected", G_CALLBACK(on_monitor_row_selected), self);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), list_box);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_vexpand(scrolled, TRUE);

    gtk_box_append(GTK_BOX(content_box), scrolled);

    if (self->section_stack) {
        GtkWidget *separator = gtk_separator_new(GTK_ORIENTATION_HORIZONTAL);
        gtk_widget_set_margin_top(separator, 12);
        gtk_box_append(GTK_BOX(content_box), separator);

        GtkWidget *sections_label = gtk_label_new("Sections");
        gtk_widget_add_css_class(sections_label, "heading");
        gtk_widget_set_halign(sections_label, GTK_ALIGN_START);
        gtk_label_set_xalign(GTK_LABEL(sections_label), 0.0f);
        gtk_widget_set_margin_top(sections_label, 12);
        gtk_box_append(GTK_BOX(content_box), sections_label);

        GtkWidget *section_switcher = adw_view_switcher_new();
        adw_view_switcher_set_stack(ADW_VIEW_SWITCHER(section_switcher), self->section_stack);
        adw_view_switcher_set_policy(ADW_VIEW_SWITCHER(section_switcher), ADW_VIEW_SWITCHER_POLICY_NARROW);
        gtk_orientable_set_orientation(GTK_ORIENTABLE(section_switcher), GTK_ORIENTATION_VERTICAL);
        gtk_widget_set_margin_bottom(section_switcher, 12);
        gtk_widget_set_halign(section_switcher, GTK_ALIGN_FILL);
        gtk_widget_set_valign(section_switcher, GTK_ALIGN_START);
        gtk_box_append(GTK_BOX(content_box), section_switcher);
    }

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), content_box);

    self->monitor_list = GTK_LIST_BOX(list_box);

    return toolbar_view;
}

static GtkWidget *build_detail_panel(AppWindow *self) {
    GtkWidget *content_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_top(content_box, 24);
    gtk_widget_set_margin_bottom(content_box, 24);
    gtk_widget_set_margin_start(content_box, 24);
    gtk_widget_set_margin_end(content_box, 24);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_hexpand(title_row, TRUE);

    self->title_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->title_label), "title-2");
    gtk_widget_set_hexpand(GTK_WIDGET(self->title_label), TRUE);
    gtk_label_set_xalign(self->title_label, 0.0f);
    gtk_box_append(GTK_BOX(title_row), GTK_WIDGET(self->title_label));

    self->rename_button = GTK_BUTTON(gtk_button_new_from_icon_name("document-edit-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->rename_button), "flat");
    gtk_widget_set_sensitive(GTK_WIDGET(self->rename_button), FALSE);
    gtk_box_append(GTK_BOX(title_row), GTK_WIDGET(self->rename_button));

    gtk_box_append(GTK_BOX(title_box), title_row);

    self->subtitle_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->subtitle_label), "dim-label");
    gtk_label_set_xalign(self->subtitle_label, 0.0f);
    gtk_box_append(GTK_BOX(title_box), GTK_WIDGET(self->subtitle_label));
    gtk_box_append(GTK_BOX(content_box), title_box);

    g_signal_connect(self->rename_button, "clicked", G_CALLBACK(on_rename_button_clicked), self);

    if (!self->slider_size_group) {
        self->slider_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);
    }

    self->section_stack = ADW_VIEW_STACK(adw_view_stack_new());
    gtk_widget_set_hexpand(GTK_WIDGET(self->section_stack), TRUE);
    gtk_widget_set_vexpand(GTK_WIDGET(self->section_stack), TRUE);

    GtkWidget *picture_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *audio_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
    GtkWidget *details_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);

    gtk_widget_set_margin_top(picture_page, 12);
    gtk_widget_set_margin_top(audio_page, 12);
    gtk_widget_set_margin_top(details_page, 12);

    typedef struct {
        const gchar *id;
        GtkWidget *group;
        GtkWidget *page;
        gboolean appended;
    } GroupInfo;

    GroupInfo groups[G_N_ELEMENTS(picture_group_specs)] = {0};
    for (guint i = 0; i < G_N_ELEMENTS(picture_group_specs); i++) {
        groups[i].id = picture_group_specs[i].id;
        groups[i].group = adw_preferences_group_new();
        if (picture_group_specs[i].title) {
            adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(groups[i].group), picture_group_specs[i].title);
        }
        if (g_strcmp0(groups[i].id, "audio") == 0) {
            groups[i].page = audio_page;
        } else {
            groups[i].page = picture_page;
        }
    }

    for (guint i = 0; i < PICTURE_CONTROL_COUNT; i++) {
        GtkWidget *target_group = NULL;
        for (guint g = 0; g < G_N_ELEMENTS(groups); g++) {
            if (g_strcmp0(groups[g].id, picture_control_specs[i].group_id) == 0) {
                if (!groups[g].appended) {
                    if (groups[g].page) {
                        gtk_box_append(GTK_BOX(groups[g].page), groups[g].group);
                    }
                    groups[g].appended = TRUE;
                }
                target_group = groups[g].group;
                break;
            }
        }

        GtkAdjustment *adjustment = gtk_adjustment_new(0, 0, 100, 1, 5, 0);
        GtkScale *scale = GTK_SCALE(gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment));
        gtk_scale_set_draw_value(scale, FALSE);
        gtk_scale_set_digits(scale, 0);
        gtk_widget_set_hexpand(GTK_WIDGET(scale), TRUE);
        gtk_widget_set_valign(GTK_WIDGET(scale), GTK_ALIGN_CENTER);
        gtk_widget_set_margin_end(GTK_WIDGET(scale), 8);
        gtk_widget_set_margin_start(GTK_WIDGET(scale), 0);

        GtkLabel *value_label = GTK_LABEL(gtk_label_new("—"));
        gtk_widget_add_css_class(GTK_WIDGET(value_label), "dim-label");
        gtk_widget_set_valign(GTK_WIDGET(value_label), GTK_ALIGN_CENTER);
        gtk_widget_set_halign(GTK_WIDGET(value_label), GTK_ALIGN_END);
        gtk_label_set_xalign(value_label, 1.0f);
        gtk_label_set_width_chars(value_label, 4);

        GtkWidget *suffix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
        gtk_widget_set_hexpand(suffix, TRUE);
        gtk_widget_set_halign(suffix, GTK_ALIGN_FILL);
        gtk_widget_set_margin_start(suffix, 0);
        gtk_box_append(GTK_BOX(suffix), GTK_WIDGET(scale));
        gtk_box_append(GTK_BOX(suffix), GTK_WIDGET(value_label));
        gtk_size_group_add_widget(self->slider_size_group, GTK_WIDGET(scale));

        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        gtk_widget_add_css_class(GTK_WIDGET(row), "flat");
        g_object_set(row, "activatable", FALSE, NULL);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), picture_control_specs[i].title);
        adw_action_row_add_suffix(row, suffix);
        gtk_widget_set_hexpand(GTK_WIDGET(row), TRUE);

        if (target_group) {
            adw_preferences_group_add(ADW_PREFERENCES_GROUP(target_group), GTK_WIDGET(row));
        }

        PictureControl *control = &self->picture_controls[i];
        control->app = self;
        control->scale = scale;
        control->value_label = value_label;
        control->row = GTK_WIDGET(row);
        control->vcp_code = picture_control_specs[i].vcp_code;
        control->feedback_prefix = picture_control_specs[i].feedback_prefix;
        control->max = 100;
        control->pending_value = 0;
        control->last_sent_value = -1;
        control->updating = FALSE;
        control->dragging = FALSE;

        picture_control_clear(control);

        g_signal_connect(scale, "value-changed", G_CALLBACK(on_picture_value_changed), control);

        GtkGesture *drag = GTK_GESTURE(gtk_gesture_drag_new());
        gtk_widget_add_controller(GTK_WIDGET(scale), GTK_EVENT_CONTROLLER(drag));
        g_signal_connect(drag, "drag-begin", G_CALLBACK(on_picture_drag_begin), control);
        g_signal_connect(drag, "drag-end", G_CALLBACK(on_picture_drag_end), control);
        g_signal_connect(drag, "cancel", G_CALLBACK(on_picture_drag_cancel), control);
    }

    self->feedback_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->feedback_label), "dim-label");
    gtk_label_set_xalign(self->feedback_label, 0.0f);
    gtk_box_append(GTK_BOX(picture_page), GTK_WIDGET(self->feedback_label));

    GtkWidget *info_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(info_group), "Details");

    self->bus_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->bus_row), "I2C bus");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->bus_row));

    self->serial_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->serial_row), "Serial number");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->serial_row));

    self->manufacturer_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->manufacturer_row), "Manufacturer");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->manufacturer_row));

    self->mccs_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->mccs_row), "MCCS version");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->mccs_row));

    self->firmware_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->firmware_row), "Firmware");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->firmware_row));

    self->manufacture_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->manufacture_row), "Manufactured");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->manufacture_row));

    gtk_box_append(GTK_BOX(details_page), info_group);

    AdwViewStackPage *picture_stack_page = adw_view_stack_add_named(self->section_stack, picture_page, "picture");
    adw_view_stack_page_set_title(picture_stack_page, "Picture");

    AdwViewStackPage *audio_stack_page = adw_view_stack_add_named(self->section_stack, audio_page, "audio");
    adw_view_stack_page_set_title(audio_stack_page, "Audio");

    AdwViewStackPage *details_stack_page = adw_view_stack_add_named(self->section_stack, details_page, "details");
    adw_view_stack_page_set_title(details_stack_page, "Details");

    gtk_box_append(GTK_BOX(content_box), GTK_WIDGET(self->section_stack));

    GtkWidget *clamp = adw_clamp_new();
    adw_clamp_set_child(ADW_CLAMP(clamp), content_box);

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_hexpand(scrolled, TRUE);
    gtk_widget_set_vexpand(scrolled, TRUE);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), clamp);

    return scrolled;
}

static void app_window_show_monitor(AppWindow *self, MonitorItem *item) {
    gtk_stack_set_visible_child_name(self->content_stack, "detail");
    app_window_set_feedback(self, "Loading picture controls…");

    if (self->section_stack) {
        adw_view_stack_set_visible_child_name(self->section_stack, "picture");
    }

    app_window_update_identity(self, item);

    gtk_label_set_text(self->title_label, monitor_item_get_name(item));
    gchar *subtitle = g_strdup_printf("Display %s", monitor_item_get_display_id(item));
    gtk_label_set_text(self->subtitle_label, subtitle);
    g_free(subtitle);

    const gchar *bus = monitor_item_get_bus(item);
    adw_action_row_set_subtitle(self->bus_row, (bus && *bus) ? bus : "Not reported");

    const gchar *serial = monitor_item_get_serial(item);
    adw_action_row_set_subtitle(self->serial_row, (serial && *serial) ? serial : "Not reported");

    const gchar *manufacturer = monitor_item_get_manufacturer(item);
    adw_action_row_set_subtitle(self->manufacturer_row,
                                (manufacturer && *manufacturer) ? manufacturer : "Not reported");

    const gchar *mccs = monitor_item_get_mccs_version(item);
    adw_action_row_set_subtitle(self->mccs_row, (mccs && *mccs) ? mccs : "Not reported");

    const gchar *firmware = monitor_item_get_firmware(item);
    adw_action_row_set_subtitle(self->firmware_row, (firmware && *firmware) ? firmware : "Not reported");

    const gchar *manufactured = monitor_item_get_manufacture_date(item);
    adw_action_row_set_subtitle(self->manufacture_row,
                                (manufactured && *manufactured) ? manufactured : "Not reported");

    app_window_start_picture_load(self, item);
}

static gboolean is_cancelled_error(const GError *error) {
    return error && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_CANCELLED);
}

static void app_window_start_picture_load(AppWindow *self, MonitorItem *item) {
    app_window_cancel_load(self);

    self->load_cancellable = g_cancellable_new();
    g_clear_pointer(&self->loading_display_id, g_free);
    self->loading_display_id = g_strdup(monitor_item_get_display_id(item));
    self->load_generation++;

    GTask *task = g_task_new(self->window, self->load_cancellable, picture_load_task_complete, NULL);

    PictureLoadTaskData *data = g_new0(PictureLoadTaskData, 1);
    data->display_id = g_strdup(self->loading_display_id);
    data->n_codes = PICTURE_CONTROL_COUNT;
    data->codes = g_new0(guint8, data->n_codes);
    for (guint i = 0; i < data->n_codes; i++) {
        data->codes[i] = picture_control_specs[i].vcp_code;
    }
    data->generation = self->load_generation;

    g_task_set_task_data(task, data, (GDestroyNotify)picture_load_task_data_free);
    g_task_run_in_thread(task, (GTaskThreadFunc)picture_load_task_thread);
    g_object_unref(task);
}

static void picture_load_task_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    PictureLoadTaskData *data = task_data;
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
        return;
    }

    DdcutilVcpValue *values = g_new0(DdcutilVcpValue, data->n_codes);
    GError *error = NULL;
    if (!ddcutil_get_multiple_vcp_values(data->display_id, data->codes, data->n_codes, values, &error)) {
        for (guint i = 0; i < data->n_codes; i++) {
            g_free(values[i].error_message);
        }
        g_free(values);
        if (!error) {
            error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to read picture controls");
        }
        g_task_return_error(task, error);
        return;
    }

    if (g_cancellable_is_cancelled(cancellable)) {
        for (guint i = 0; i < data->n_codes; i++) {
            g_free(values[i].error_message);
        }
        g_free(values);
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
        return;
    }

    PictureLoadResult *result = g_new0(PictureLoadResult, 1);
    result->display_id = g_strdup(data->display_id);
    result->n_codes = data->n_codes;
    result->values = values;
    result->generation = data->generation;

    g_task_return_pointer(task, result, (GDestroyNotify)picture_load_result_free);
}

static void apply_picture_values(AppWindow *self, PictureLoadResult *result) {
    gchar *first_error = NULL;

    for (guint i = 0; i < result->n_codes && i < PICTURE_CONTROL_COUNT; i++) {
        PictureControl *control = &self->picture_controls[i];
        if (result->values[i].success) {
            picture_control_apply_loaded_value(control, result->values[i].current, result->values[i].maximum);
        } else {
            picture_control_clear(control);
            if (!first_error) {
                if (result->values[i].error_message && *result->values[i].error_message) {
                    first_error = g_strdup(result->values[i].error_message);
                } else {
                    first_error = g_strdup_printf("%s control unavailable for this display.",
                                                   picture_control_specs[i].feedback_prefix);
                }
            }
        }
    }

    if (first_error) {
        app_window_set_feedback(self, first_error);
        g_free(first_error);
    } else {
        app_window_set_feedback(self, "");
    }
}

static void picture_load_task_complete(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    AppWindow *self = g_object_get_data(source_object, "app-window-state");
    if (!self) {
        return;
    }

    if (self->load_cancellable) {
        g_clear_object(&self->load_cancellable);
    }

    GError *error = NULL;
    PictureLoadResult *values = g_task_propagate_pointer(G_TASK(result), &error);

    if (!values) {
        if (error) {
            if (!is_cancelled_error(error)) {
                app_window_set_feedback(self, error->message);
            }
            g_error_free(error);
        }
        return;
    }

    gboolean apply = TRUE;
    if (!self->loading_display_id || g_strcmp0(self->loading_display_id, values->display_id) != 0) {
        apply = FALSE;
    } else if (values->generation != self->load_generation) {
        apply = FALSE;
    }

    if (apply) {
        apply_picture_values(self, values);
    }

    picture_load_result_free(values);
}

static void refresh_task_thread(GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable) {
    if (g_cancellable_is_cancelled(cancellable)) {
        g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_CANCELLED, "Cancelled");
        return;
    }

    GError *error = NULL;
    GPtrArray *monitors = ddcutil_list_monitors(&error);

    if (!monitors) {
        if (!error) {
            error = g_error_new(G_IO_ERROR, G_IO_ERROR_FAILED, "Unable to detect displays");
        }
        g_task_return_error(task, error);
        return;
    }

    g_task_return_pointer(task, monitors, (GDestroyNotify)g_ptr_array_unref);
}

static void refresh_task_complete(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    AppWindow *self = g_object_get_data(source_object, "app-window-state");
    if (!self) {
        return;
    }

    if (self->refresh_cancellable) {
        g_clear_object(&self->refresh_cancellable);
    }

    gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_button), TRUE);

    GError *error = NULL;
    GPtrArray *monitors = g_task_propagate_pointer(G_TASK(result), &error);

    g_list_store_remove_all(self->monitor_store);
    self->current_position = GTK_INVALID_LIST_POSITION;
    app_window_cancel_load(self);
    app_window_reset_picture_controls(self);

    if (!monitors) {
        if (error) {
            if (!is_cancelled_error(error)) {
                app_window_set_content_status(self,
                                              "computer-fail-symbolic",
                                              "Unable to detect displays",
                                              error->message);
            }
            g_error_free(error);
        }
        return;
    }

    for (guint i = 0; i < monitors->len; i++) {
        DdcutilMonitor *monitor = g_ptr_array_index(monitors, i);
        MonitorItem *item = monitor_item_new(monitor->display_id,
                                             monitor->name,
                                             monitor->bus,
                                             monitor->serial,
                                             monitor->manufacturer,
                                             monitor->mccs_version,
                                             monitor->firmware_version,
                                             monitor->manufacture_date);
        g_list_store_append(self->monitor_store, item);
        g_object_unref(item);
    }
    g_ptr_array_unref(monitors);

    if (g_list_model_get_n_items(G_LIST_MODEL(self->monitor_store)) == 0) {
        app_window_set_content_status(self,
                                      "computer-fail-symbolic",
                                      "No DDC displays found",
                                      "Make sure your monitors expose DDC/CI and that you have access to /dev/i2c-*.");
        return;
    }

    app_window_select_position(self, 0);
}

static void app_window_refresh(AppWindow *self) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_button), FALSE);
    app_window_set_content_status(self,
                                  "view-refresh-symbolic",
                                  "Detecting displays",
                                  "Querying ddcutil for connected monitors…");

    app_window_cancel_refresh(self);
    self->refresh_cancellable = g_cancellable_new();

    GTask *task = g_task_new(self->window, self->refresh_cancellable, refresh_task_complete, NULL);
    g_task_run_in_thread(task, (GTaskThreadFunc)refresh_task_thread);
    g_object_unref(task);
}

static AppWindow *app_window_new(GtkApplication *app) {
    AppWindow *self = g_new0(AppWindow, 1);

    self->window = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(self->window), "GnomeDDC");
    gtk_window_set_default_size(GTK_WINDOW(self->window), 900, 600);

    self->monitor_store = g_list_store_new(MONITOR_TYPE_ITEM);
    self->current_position = GTK_INVALID_LIST_POSITION;

    self->refresh_button = GTK_BUTTON(gtk_button_new_from_icon_name("view-refresh-symbolic"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->refresh_button), "Re-detect connected monitors");
    g_signal_connect(self->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), self);

    self->content_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(self->content_stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    GtkWidget *status_page = adw_status_page_new();
    self->status_page = ADW_STATUS_PAGE(status_page);
    gtk_stack_add_named(self->content_stack, status_page, "status");

    GtkWidget *detail_panel = build_detail_panel(self);
    gtk_stack_add_named(self->content_stack, detail_panel, "detail");

    GtkWidget *sidebar_widget = build_sidebar(self);

    GtkWidget *split_view = adw_navigation_split_view_new();
    gtk_widget_set_hexpand(split_view, TRUE);
    gtk_widget_set_vexpand(split_view, TRUE);
    adw_navigation_split_view_set_sidebar_width_fraction(ADW_NAVIGATION_SPLIT_VIEW(split_view), 0.28);

    AdwNavigationPage *sidebar_page = adw_navigation_page_new(sidebar_widget, "Displays");
    adw_navigation_split_view_set_sidebar(ADW_NAVIGATION_SPLIT_VIEW(split_view), sidebar_page);

    GtkWidget *content_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(content_wrapper, TRUE);
    gtk_widget_set_vexpand(content_wrapper, TRUE);
    gtk_box_append(GTK_BOX(content_wrapper), GTK_WIDGET(self->content_stack));

    GtkWidget *content_toolbar = adw_toolbar_view_new();
    GtkWidget *content_header = adw_header_bar_new();
    GtkWidget *content_title = adw_window_title_new("Displays", NULL);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(content_header), content_title);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(content_toolbar), content_header);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(content_toolbar), content_wrapper);

    AdwNavigationPage *content_page = adw_navigation_page_new(content_toolbar, "Details");
    adw_navigation_split_view_set_content(ADW_NAVIGATION_SPLIT_VIEW(split_view), content_page);

    GtkWidget *breakpoint_bin = adw_breakpoint_bin_new();
    adw_breakpoint_bin_set_child(ADW_BREAKPOINT_BIN(breakpoint_bin), split_view);

    AdwBreakpointCondition *condition = adw_breakpoint_condition_parse("max-width: 720sp");
    AdwBreakpoint *breakpoint = adw_breakpoint_new(condition);
    GValue collapsed = G_VALUE_INIT;
    g_value_init(&collapsed, G_TYPE_BOOLEAN);
    g_value_set_boolean(&collapsed, TRUE);
    adw_breakpoint_add_setter(breakpoint, G_OBJECT(split_view), "collapsed", &collapsed);
    g_value_unset(&collapsed);
    adw_breakpoint_bin_add_breakpoint(ADW_BREAKPOINT_BIN(breakpoint_bin), breakpoint);
    g_object_unref(breakpoint);

    adw_application_window_set_content(self->window, breakpoint_bin);

    if (self->section_stack) {
        adw_view_stack_set_visible_child_name(self->section_stack, "picture");
    }

    g_object_set_data_full(G_OBJECT(self->window), "app-window-state", self, (GDestroyNotify)app_window_free);

    app_window_set_content_status(self,
                                  "view-refresh-symbolic",
                                  "Detecting displays",
                                  "Querying ddcutil for connected monitors…");

    return self;
}

static void on_activate(GtkApplication *app, gpointer user_data) {
    AppWindow *window_state = app_window_new(app);
    app_window_refresh(window_state);
    GtkSettings *settings = gtk_settings_get_default();
    if (settings) {
        if (!color_scheme_watched) {
            g_signal_connect(settings,
                             "notify::gtk-application-prefer-dark-theme",
                             G_CALLBACK(on_prefer_dark_changed),
                             app);
            color_scheme_watched = TRUE;
        }
        update_color_scheme(ADW_APPLICATION(app));
    }
    gtk_window_present(GTK_WINDOW(window_state->window));
}

static void update_color_scheme(AdwApplication *app) {
    GtkSettings *settings = gtk_settings_get_default();
    if (!settings) {
        return;
    }

    gboolean prefer_dark = FALSE;
    g_object_get(settings, "gtk-application-prefer-dark-theme", &prefer_dark, NULL);

    AdwStyleManager *style_manager = adw_application_get_style_manager(app);
    adw_style_manager_set_color_scheme(style_manager,
                                       prefer_dark ? ADW_COLOR_SCHEME_PREFER_DARK : ADW_COLOR_SCHEME_DEFAULT);

    g_object_set(settings, "gtk-application-prefer-dark-theme", FALSE, NULL);
}

static void on_prefer_dark_changed(GObject *settings, GParamSpec *pspec, gpointer user_data) {
    update_color_scheme(ADW_APPLICATION(user_data));
    (void)settings;
    (void)pspec;
}

int main(int argc, char *argv[]) {
    g_autoptr(AdwApplication) app = adw_application_new("dev.gnomeddc", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
