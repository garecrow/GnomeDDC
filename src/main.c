#include <adwaita.h>
#include <gtk/gtk.h>

#include "ddcutil_client.h"
#include "monitor_item.h"

typedef struct _AppWindow AppWindow;

typedef enum {
    PICTURE_CONTROL_BRIGHTNESS = 0,
    PICTURE_CONTROL_CONTRAST,
    PICTURE_CONTROL_RED_GAIN,
    PICTURE_CONTROL_GREEN_GAIN,
    PICTURE_CONTROL_BLUE_GAIN,
    PICTURE_CONTROL_COUNT
} PictureControlType;

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

struct _AppWindow {
    AdwApplicationWindow *window;
    GtkButton *refresh_button;
    GtkStack *content_stack;
    AdwStatusPage *status_page;
    GtkLabel *title_label;
    GtkLabel *subtitle_label;
    GtkLabel *feedback_label;
    AdwActionRow *bus_row;
    AdwActionRow *serial_row;
    GListStore *monitor_store;
    GtkListBox *monitor_list;
    guint current_position;
    PictureControl picture_controls[PICTURE_CONTROL_COUNT];
    gboolean suppress_selection_signal;
};

static const struct {
    const gchar *title;
    guint8 vcp_code;
    const gchar *feedback_prefix;
} picture_control_specs[PICTURE_CONTROL_COUNT] = {
    [PICTURE_CONTROL_BRIGHTNESS] = {"Brightness", 0x10, "Brightness"},
    [PICTURE_CONTROL_CONTRAST] = {"Contrast", 0x12, "Contrast"},
    [PICTURE_CONTROL_RED_GAIN] = {"Red", 0x16, "Red"},
    [PICTURE_CONTROL_GREEN_GAIN] = {"Green", 0x18, "Green"},
    [PICTURE_CONTROL_BLUE_GAIN] = {"Blue", 0x1A, "Blue"},
};

static void app_window_refresh(AppWindow *self);
static void app_window_show_monitor(AppWindow *self, MonitorItem *item);
static void app_window_set_feedback(AppWindow *self, const gchar *message);
static void app_window_reset_picture_controls(AppWindow *self);
static void picture_control_clear(PictureControl *control);
static void picture_control_enable(PictureControl *control);
static void picture_control_update_label(PictureControl *control, gdouble value);
static void picture_control_apply(PictureControl *control, gint new_value);
static gboolean picture_control_load(PictureControl *control, MonitorItem *item, gchar **error_message);
static void on_picture_drag_begin(GtkGestureDrag *gesture, gdouble start_x, gdouble start_y, gpointer user_data);
static void on_picture_drag_end(GtkGestureDrag *gesture, gdouble offset_x, gdouble offset_y, gpointer user_data);
static void on_picture_drag_cancel(GtkGesture *gesture, GdkEventSequence *sequence, gpointer user_data);
static void on_picture_value_changed(GtkRange *range, gpointer user_data);
static void on_prefer_dark_changed(GObject *settings, GParamSpec *pspec, gpointer user_data);
static void update_color_scheme(AdwApplication *app);
static gboolean color_scheme_watched = FALSE;

static void app_window_free(AppWindow *self) {
    if (!self) {
        return;
    }
    g_clear_object(&self->monitor_store);
    g_free(self);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    AppWindow *self = user_data;
    app_window_refresh(self);
}

static void app_window_set_feedback(AppWindow *self, const gchar *message) {
    gtk_label_set_text(self->feedback_label, message ? message : "");
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

static gboolean picture_control_load(PictureControl *control, MonitorItem *item, gchar **error_message) {
    if (error_message) {
        *error_message = NULL;
    }

    if (!control || !item) {
        return FALSE;
    }

    const gchar *display_id = monitor_item_get_display_id(item);
    gint current = 0;
    gint maximum = 100;
    GError *error = NULL;
    if (!ddcutil_get_vcp_value(display_id, control->vcp_code, &current, &maximum, &error)) {
        picture_control_clear(control);
        if (error_message) {
            if (error && error->message) {
                *error_message = g_strdup(error->message);
            } else {
                *error_message = g_strdup_printf("%s control unavailable for this display.", control->feedback_prefix);
            }
        }
        if (error) {
            g_error_free(error);
        }
        return FALSE;
    }

    control->max = maximum > 0 ? maximum : 100;
    picture_control_enable(control);
    control->updating = TRUE;
    gtk_range_set_range(GTK_RANGE(control->scale), 0, control->max);
    gtk_range_set_value(GTK_RANGE(control->scale), current);
    control->pending_value = current;
    control->last_sent_value = current;
    control->dragging = FALSE;
    control->updating = FALSE;
    picture_control_update_label(control, current);
    return TRUE;
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

static GtkWidget *create_sidebar_row(gpointer item, gpointer user_data) {
    MonitorItem *monitor = MONITOR_ITEM(item);
    GtkWidget *row = adw_action_row_new();
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), monitor_item_get_name(monitor));

    const gchar *bus = monitor_item_get_bus(monitor);
    if (bus && *bus) {
        adw_action_row_set_subtitle(ADW_ACTION_ROW(row), bus);
    }

    g_object_set_data_full(G_OBJECT(row), "monitor-item", g_object_ref(monitor), g_object_unref);

    return row;
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
        app_window_reset_picture_controls(self);
        adw_status_page_set_icon_name(self->status_page, "computer-symbolic");
        adw_status_page_set_title(self->status_page, "No monitor selected");
        adw_status_page_set_description(self->status_page,
                                        "Choose a display from the sidebar to adjust its picture settings.");
        gtk_stack_set_visible_child_name(self->content_stack, "status");
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
    if (self->current_position == index) {
        return;
    }

    app_window_select_position(self, index);
    (void)box;
}

static GtkWidget *build_sidebar(AppWindow *self) {
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar, "navigation-sidebar");

    GtkWidget *title_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(title_wrapper, 32);
    gtk_widget_set_margin_bottom(title_wrapper, 24);
    gtk_widget_set_margin_start(title_wrapper, 28);
    gtk_widget_set_margin_end(title_wrapper, 28);

    GtkWidget *category_label = gtk_label_new("Displays");
    gtk_widget_add_css_class(category_label, "heading");
    gtk_widget_set_halign(category_label, GTK_ALIGN_START);
    gtk_widget_set_valign(category_label, GTK_ALIGN_CENTER);
    gtk_label_set_xalign(GTK_LABEL(category_label), 0.0f);
    gtk_box_append(GTK_BOX(title_wrapper), category_label);

    gtk_box_append(GTK_BOX(sidebar), title_wrapper);

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

    gtk_box_append(GTK_BOX(sidebar), scrolled);

    self->monitor_list = GTK_LIST_BOX(list_box);

    return sidebar;
}

static GtkWidget *build_detail_panel(AppWindow *self) {
    GtkWidget *detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_top(detail_box, 24);
    gtk_widget_set_margin_bottom(detail_box, 24);
    gtk_widget_set_margin_start(detail_box, 24);
    gtk_widget_set_margin_end(detail_box, 24);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    self->title_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->title_label), "title-2");
    gtk_label_set_xalign(self->title_label, 0.0f);
    gtk_box_append(GTK_BOX(title_box), GTK_WIDGET(self->title_label));

    self->subtitle_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->subtitle_label), "dim-label");
    gtk_label_set_xalign(self->subtitle_label, 0.0f);
    gtk_box_append(GTK_BOX(title_box), GTK_WIDGET(self->subtitle_label));
    gtk_box_append(GTK_BOX(detail_box), title_box);

    GtkWidget *controls_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(controls_group), "Picture");

    for (guint i = 0; i < PICTURE_CONTROL_COUNT; i++) {
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

        AdwActionRow *row = ADW_ACTION_ROW(adw_action_row_new());
        gtk_widget_add_css_class(GTK_WIDGET(row), "flat");
        g_object_set(row, "activatable", FALSE, NULL);
        adw_preferences_row_set_title(ADW_PREFERENCES_ROW(row), picture_control_specs[i].title);
        adw_action_row_add_suffix(row, suffix);
        gtk_widget_set_hexpand(GTK_WIDGET(row), TRUE);
        if (i == PICTURE_CONTROL_BRIGHTNESS || i == PICTURE_CONTROL_CONTRAST) {
            gtk_widget_set_margin_bottom(GTK_WIDGET(row), 12);
        }
        adw_preferences_group_add(ADW_PREFERENCES_GROUP(controls_group), GTK_WIDGET(row));

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

    gtk_box_append(GTK_BOX(detail_box), controls_group);

    self->feedback_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->feedback_label), "dim-label");
    gtk_label_set_xalign(self->feedback_label, 0.0f);
    gtk_box_append(GTK_BOX(detail_box), GTK_WIDGET(self->feedback_label));

    GtkWidget *info_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(info_group), "Details");

    self->bus_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->bus_row), "I2C bus");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->bus_row));

    self->serial_row = ADW_ACTION_ROW(adw_action_row_new());
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(self->serial_row), "Serial number");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(info_group), GTK_WIDGET(self->serial_row));

    gtk_box_append(GTK_BOX(detail_box), info_group);

    GtkWidget *clamp = adw_clamp_new();
    gtk_widget_set_hexpand(clamp, TRUE);
    gtk_widget_set_vexpand(clamp, TRUE);
    adw_clamp_set_child(ADW_CLAMP(clamp), detail_box);

    return clamp;
}

static void app_window_show_status(AppWindow *self, const gchar *icon, const gchar *title, const gchar *description) {
    adw_status_page_set_icon_name(self->status_page, icon);
    adw_status_page_set_title(self->status_page, title);
    adw_status_page_set_description(self->status_page, description);
    gtk_stack_set_visible_child_name(self->content_stack, "status");
}

static void app_window_show_monitor(AppWindow *self, MonitorItem *item) {
    gtk_stack_set_visible_child_name(self->content_stack, "detail");
    app_window_set_feedback(self, "");

    gtk_label_set_text(self->title_label, monitor_item_get_name(item));
    gchar *subtitle = g_strdup_printf("Display %s", monitor_item_get_display_id(item));
    gtk_label_set_text(self->subtitle_label, subtitle);
    g_free(subtitle);

    const gchar *bus = monitor_item_get_bus(item);
    adw_action_row_set_subtitle(self->bus_row, (bus && *bus) ? bus : "Not reported");

    const gchar *serial = monitor_item_get_serial(item);
    adw_action_row_set_subtitle(self->serial_row, (serial && *serial) ? serial : "Not reported");

    gchar *first_error = NULL;
    for (guint i = 0; i < PICTURE_CONTROL_COUNT; i++) {
        PictureControl *control = &self->picture_controls[i];
        gchar *error_message = NULL;
        if (!picture_control_load(control, item, &error_message)) {
            if (!first_error && error_message) {
                first_error = error_message;
            } else {
                g_free(error_message);
            }
        }
    }

    if (first_error) {
        app_window_set_feedback(self, first_error);
        g_free(first_error);
    }
}

static void app_window_refresh(AppWindow *self) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_button), FALSE);
    app_window_show_status(self, "view-refresh-symbolic", "Detecting displays", "Querying ddcutil for connected monitors…");

    g_list_store_remove_all(self->monitor_store);
    if (self->monitor_list) {
        self->suppress_selection_signal = TRUE;
        gtk_list_box_unselect_all(self->monitor_list);
        self->suppress_selection_signal = FALSE;
    }
    self->current_position = GTK_INVALID_LIST_POSITION;
    app_window_reset_picture_controls(self);

    GError *error = NULL;
    GPtrArray *monitors = ddcutil_list_monitors(&error);

    gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_button), TRUE);

    if (!monitors) {
        app_window_show_status(self, "computer-fail-symbolic", "Unable to detect displays", error ? error->message : "ddcutil did not return any displays.");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    for (guint i = 0; i < monitors->len; i++) {
        DdcutilMonitor *monitor = g_ptr_array_index(monitors, i);
        MonitorItem *item = monitor_item_new(monitor->display_id, monitor->name, monitor->bus, monitor->serial);
        g_list_store_append(self->monitor_store, item);
        g_object_unref(item);
    }
    g_ptr_array_free(monitors, TRUE);

    if (g_list_model_get_n_items(G_LIST_MODEL(self->monitor_store)) == 0) {
        app_window_show_status(self, "computer-fail-symbolic", "No DDC displays found", "Make sure your monitors expose DDC/CI and that you have permission to access /dev/i2c-*.");
        return;
    }

    app_window_select_position(self, 0);
}

static AppWindow *app_window_new(GtkApplication *app) {
    AppWindow *self = g_new0(AppWindow, 1);

    self->window = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(self->window), "GnomeDDC");
    gtk_window_set_default_size(GTK_WINDOW(self->window), 900, 600);

    self->monitor_store = g_list_store_new(MONITOR_TYPE_ITEM);
    self->current_position = GTK_INVALID_LIST_POSITION;

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    GtkWidget *header_bar = adw_header_bar_new();
    gtk_widget_add_css_class(header_bar, "flat");

    GtkWidget *window_title = adw_window_title_new("GnomeDDC", NULL);
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_bar), window_title);

    self->refresh_button = GTK_BUTTON(gtk_button_new_from_icon_name("view-refresh-symbolic"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->refresh_button), "Re-detect connected monitors");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), GTK_WIDGET(self->refresh_button));
    g_signal_connect(self->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), self);

    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header_bar);

    GtkWidget *split_view = adw_navigation_split_view_new();
    adw_navigation_split_view_set_sidebar_width_fraction(ADW_NAVIGATION_SPLIT_VIEW(split_view), 0.28);

    AdwNavigationPage *sidebar_page = adw_navigation_page_new(build_sidebar(self), "Displays");
    adw_navigation_split_view_set_sidebar(ADW_NAVIGATION_SPLIT_VIEW(split_view), sidebar_page);

    self->content_stack = GTK_STACK(gtk_stack_new());
    gtk_stack_set_transition_type(self->content_stack, GTK_STACK_TRANSITION_TYPE_CROSSFADE);

    GtkWidget *status_page = adw_status_page_new();
    self->status_page = ADW_STATUS_PAGE(status_page);
    gtk_stack_add_named(self->content_stack, status_page, "status");

    GtkWidget *detail_panel = build_detail_panel(self);
    gtk_stack_add_named(self->content_stack, detail_panel, "detail");

    GtkWidget *content_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_set_hexpand(content_wrapper, TRUE);
    gtk_widget_set_vexpand(content_wrapper, TRUE);
    gtk_box_append(GTK_BOX(content_wrapper), GTK_WIDGET(self->content_stack));

    AdwNavigationPage *content_page = adw_navigation_page_new(content_wrapper, "Details");
    adw_navigation_split_view_set_content(ADW_NAVIGATION_SPLIT_VIEW(split_view), content_page);

    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar_view), split_view);
    adw_application_window_set_content(self->window, toolbar_view);

    g_object_set_data_full(G_OBJECT(self->window), "app-window-state", self, (GDestroyNotify)app_window_free);

    app_window_show_status(self, "view-refresh-symbolic", "Detecting displays", "Querying ddcutil for connected monitors…");

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
    adw_style_manager_set_color_scheme(style_manager, prefer_dark ? ADW_COLOR_SCHEME_PREFER_DARK : ADW_COLOR_SCHEME_DEFAULT);

    if (prefer_dark) {
        g_object_set(settings, "gtk-application-prefer-dark-theme", FALSE, NULL);
    }
}

static void on_prefer_dark_changed(GObject *settings, GParamSpec *pspec, gpointer user_data) {
    update_color_scheme(ADW_APPLICATION(user_data));
}

int main(int argc, char *argv[]) {
    g_autoptr(AdwApplication) app = adw_application_new("dev.gnomeddc", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
