#include <adwaita.h>
#include <gtk/gtk.h>

#include "ddcutil_client.h"
#include "monitor_item.h"

typedef struct _AppWindow AppWindow;

typedef struct {
    AppWindow *window;
    GtkScale *scale;
    GtkLabel *value_label;
    gint maximum;
    gboolean updating;
    const gchar *vcp_code;
    const gchar *feedback_label;
} PictureControlBinding;

typedef struct {
    AppWindow *window;
    MonitorItem *item;
} RenameDialogData;

struct _AppWindow {
    AdwApplicationWindow *window;
    GtkButton *refresh_button;
    GtkStack *content_stack;
    AdwStatusPage *status_page;
    GtkScale *brightness_scale;
    GtkLabel *brightness_value_label;
    GtkLabel *title_label;
    GtkLabel *subtitle_label;
    GtkLabel *feedback_label;
    AdwActionRow *bus_row;
    AdwActionRow *serial_row;
    GListStore *monitor_store;
    GtkSingleSelection *selection;
    gboolean updating_slider;
    gint brightness_max;
    GtkButton *rename_button;
    GtkSizeGroup *picture_size_group;
    PictureControlBinding contrast_control;
    PictureControlBinding red_control;
    PictureControlBinding green_control;
    PictureControlBinding blue_control;
};

static void app_window_refresh(AppWindow *self);
static void app_window_show_monitor(AppWindow *self, MonitorItem *item);
static void app_window_set_feedback(AppWindow *self, const gchar *message);
static void app_window_update_brightness_label(AppWindow *self, gdouble value);
static void on_prefer_dark_changed(GObject *settings, GParamSpec *pspec, gpointer user_data);
static void update_color_scheme(AdwApplication *app);
static void ensure_app_styles(void);
static void on_rename_button_clicked(GtkButton *button, gpointer user_data);
static void on_rename_dialog_chosen(GObject *source_object, GAsyncResult *result, gpointer user_data);
static void on_rename_entry_activated(GtkEditable *editable, gpointer user_data);
static gboolean app_window_apply_rename(AppWindow *self, MonitorItem *item, const gchar *name);
static void rename_dialog_data_free(RenameDialogData *data);
static void picture_control_init_binding(PictureControlBinding *binding,
                                         AppWindow *self,
                                         GtkScale *scale,
                                         GtkLabel *value_label,
                                         const gchar *vcp_code,
                                         const gchar *feedback_label);
static void picture_control_update_label(PictureControlBinding *binding, gint value);
static void picture_control_set_enabled(PictureControlBinding *binding, gboolean enabled);
static void picture_control_load_value(PictureControlBinding *binding, const gchar *display_id);
static void on_picture_control_value_changed(GtkRange *range, gpointer user_data);
static gboolean color_scheme_watched = FALSE;

static void app_window_free(AppWindow *self) {
    if (!self) {
        return;
    }
    g_clear_object(&self->selection);
    g_clear_object(&self->monitor_store);
    g_clear_object(&self->picture_size_group);
    g_free(self);
}

static void on_refresh_clicked(GtkButton *button, gpointer user_data) {
    AppWindow *self = user_data;
    app_window_refresh(self);
}

static void on_selection_changed(GObject *selection, GParamSpec *pspec, gpointer user_data) {
    AppWindow *self = user_data;
    guint position = gtk_single_selection_get_selected(self->selection);
    if (position == GTK_INVALID_LIST_POSITION) {
        if (self->rename_button) {
            gtk_widget_set_sensitive(GTK_WIDGET(self->rename_button), FALSE);
        }
        adw_status_page_set_icon_name(self->status_page, "computer-symbolic");
        adw_status_page_set_title(self->status_page, "No monitor selected");
        adw_status_page_set_description(self->status_page, "Choose a display from the sidebar to adjust its brightness.");
        gtk_stack_set_visible_child_name(self->content_stack, "status");
        return;
    }

    g_autoptr(GObject) selected = g_list_model_get_item(G_LIST_MODEL(self->monitor_store), position);
    if (!selected) {
        return;
    }

    MonitorItem *item = MONITOR_ITEM(selected);
    app_window_show_monitor(self, item);
}

static void app_window_set_feedback(AppWindow *self, const gchar *message) {
    gtk_label_set_text(self->feedback_label, message ? message : "");
}

static void app_window_update_brightness_label(AppWindow *self, gdouble value) {
    if (!self->brightness_value_label) {
        return;
    }

    gint max = self->brightness_max > 0 ? self->brightness_max : 100;
    gdouble percent_value = (max > 0) ? (value * 100.0 / (gdouble)max) : 0.0;
    if (percent_value < 0.0) {
        percent_value = 0.0;
    } else if (percent_value > 100.0) {
        percent_value = 100.0;
    }

    gint percent = (gint)(percent_value + 0.5);
    gchar *label = g_strdup_printf("%d%%", percent);
    gtk_label_set_text(self->brightness_value_label, label);
    g_free(label);
}

static void picture_control_init_binding(PictureControlBinding *binding,
                                         AppWindow *self,
                                         GtkScale *scale,
                                         GtkLabel *value_label,
                                         const gchar *vcp_code,
                                         const gchar *feedback_label) {
    binding->window = self;
    binding->scale = scale;
    binding->value_label = value_label;
    binding->maximum = 100;
    binding->updating = FALSE;
    binding->vcp_code = vcp_code;
    binding->feedback_label = feedback_label;
    g_signal_connect(scale, "value-changed", G_CALLBACK(on_picture_control_value_changed), binding);
}

static void picture_control_update_label(PictureControlBinding *binding, gint value) {
    if (!binding || !binding->value_label) {
        return;
    }

    gint max = binding->maximum > 0 ? binding->maximum : 100;
    if (max <= 0) {
        max = 100;
    }

    if (value < 0) {
        value = 0;
    } else if (binding->maximum > 0 && value > binding->maximum) {
        value = binding->maximum;
    }

    gint percent = (gint)((value * 100.0 / (gdouble)max) + 0.5);
    gchar *label = g_strdup_printf("%d%%", percent);
    gtk_label_set_text(binding->value_label, label);
    g_free(label);
}

static void picture_control_set_enabled(PictureControlBinding *binding, gboolean enabled) {
    if (!binding) {
        return;
    }
    if (binding->scale) {
        gtk_widget_set_sensitive(GTK_WIDGET(binding->scale), enabled);
    }
    if (binding->value_label) {
        gtk_widget_set_sensitive(GTK_WIDGET(binding->value_label), enabled);
    }
}

static void picture_control_load_value(PictureControlBinding *binding, const gchar *display_id) {
    if (!binding || !binding->scale) {
        return;
    }

    GError *error = NULL;
    gint current = 0;
    gint maximum = 100;
    if (ddcutil_get_vcp_value(display_id, binding->vcp_code, &current, &maximum, &error)) {
        binding->updating = TRUE;
        binding->maximum = maximum > 0 ? maximum : 100;
        gtk_range_set_range(GTK_RANGE(binding->scale), 0, binding->maximum);
        gtk_range_set_value(GTK_RANGE(binding->scale), current);
        picture_control_update_label(binding, current);
        picture_control_set_enabled(binding, TRUE);
        binding->updating = FALSE;
    } else {
        picture_control_set_enabled(binding, FALSE);
        if (binding->value_label) {
            gtk_label_set_text(binding->value_label, "—");
        }
        binding->maximum = 100;
        binding->updating = FALSE;
        if (error) {
            app_window_set_feedback(binding->window, error->message);
            g_error_free(error);
        }
    }
}

static void on_picture_control_value_changed(GtkRange *range, gpointer user_data) {
    PictureControlBinding *binding = user_data;
    if (!binding) {
        return;
    }

    gint value = (gint)gtk_range_get_value(range);
    picture_control_update_label(binding, value);

    if (binding->updating) {
        return;
    }

    AppWindow *self = binding->window;
    if (!self) {
        return;
    }

    guint position = gtk_single_selection_get_selected(self->selection);
    if (position == GTK_INVALID_LIST_POSITION) {
        return;
    }

    g_autoptr(GObject) selected = g_list_model_get_item(G_LIST_MODEL(self->monitor_store), position);
    if (!selected) {
        return;
    }

    MonitorItem *item = MONITOR_ITEM(selected);

    GError *error = NULL;
    if (!ddcutil_set_vcp_value(monitor_item_get_display_id(item), binding->vcp_code, value, &error)) {
        app_window_set_feedback(self, error ? error->message : "Failed to update control");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    gint max = binding->maximum > 0 ? binding->maximum : 100;
    gint percent = max > 0 ? (gint)((value * 100.0 / (gdouble)max) + 0.5) : value;
    if (binding->feedback_label) {
        gchar *message = g_strdup_printf("%s set to %d%%", binding->feedback_label, percent);
        app_window_set_feedback(self, message);
        g_free(message);
    }
}

static void rename_dialog_data_free(RenameDialogData *data) {
    if (!data) {
        return;
    }
    g_clear_object(&data->item);
    g_free(data);
}

static gboolean app_window_apply_rename(AppWindow *self, MonitorItem *item, const gchar *name) {
    if (!self || !item || !name) {
        return FALSE;
    }

    gchar *copy = g_strdup(name);
    gchar *trimmed = g_strstrip(copy);
    if (*trimmed == '\0') {
        g_free(copy);
        return FALSE;
    }

    monitor_item_set_name(item, trimmed);
    gtk_label_set_text(self->title_label, trimmed);

    guint position = gtk_single_selection_get_selected(self->selection);
    if (position != GTK_INVALID_LIST_POSITION) {
        g_list_model_items_changed(G_LIST_MODEL(self->monitor_store), position, 1, 1);
    }

    gchar *message = g_strdup_printf("Display renamed to %s", trimmed);
    app_window_set_feedback(self, message);
    g_free(message);
    g_free(copy);
    return TRUE;
}

static void on_brightness_value_changed(GtkRange *range, gpointer user_data) {
    AppWindow *self = user_data;
    gdouble value = gtk_range_get_value(range);
    app_window_update_brightness_label(self, value);

    if (self->updating_slider) {
        return;
    }

    guint position = gtk_single_selection_get_selected(self->selection);
    if (position == GTK_INVALID_LIST_POSITION) {
        return;
    }

    g_autoptr(GObject) selected = g_list_model_get_item(G_LIST_MODEL(self->monitor_store), position);
    if (!selected) {
        return;
    }

    MonitorItem *item = MONITOR_ITEM(selected);
    gint new_value = (gint)value;

    GError *error = NULL;
    if (!ddcutil_set_brightness(monitor_item_get_display_id(item), new_value, &error)) {
        app_window_set_feedback(self, error ? error->message : "Failed to set brightness");
        if (error) {
            g_error_free(error);
        }
    } else {
        gint max = self->brightness_max > 0 ? self->brightness_max : 100;
        gint percent = max > 0 ? (gint)((new_value * 100.0 / (gdouble)max) + 0.5) : new_value;
        gchar *message = g_strdup_printf("Brightness set to %d%%", percent);
        app_window_set_feedback(self, message);
        g_free(message);
    }
}

static void on_rename_entry_activated(GtkEditable *editable, gpointer user_data) {
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(user_data);
    RenameDialogData *data = g_object_get_data(G_OBJECT(dialog), "rename-data");
    if (!data) {
        return;
    }

    const gchar *text = gtk_editable_get_text(editable);
    if (app_window_apply_rename(data->window, data->item, text)) {
        adw_dialog_force_close(ADW_DIALOG(dialog));
    }
}

static void on_rename_dialog_chosen(GObject *source_object, GAsyncResult *result, gpointer user_data) {
    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(source_object);
    RenameDialogData *data = user_data;
    if (!data) {
        return;
    }

    const char *response = adw_alert_dialog_choose_finish(dialog, result);
    if (!response || g_strcmp0(response, "rename") != 0) {
        return;
    }

    GtkWidget *entry = adw_alert_dialog_get_extra_child(dialog);
    const gchar *text = entry ? gtk_editable_get_text(GTK_EDITABLE(entry)) : NULL;
    if (app_window_apply_rename(data->window, data->item, text)) {
        adw_dialog_force_close(ADW_DIALOG(dialog));
    }
}

static void on_rename_button_clicked(GtkButton *button, gpointer user_data) {
    AppWindow *self = user_data;
    guint position = gtk_single_selection_get_selected(self->selection);
    if (position == GTK_INVALID_LIST_POSITION) {
        return;
    }

    g_autoptr(GObject) selected = g_list_model_get_item(G_LIST_MODEL(self->monitor_store), position);
    if (!selected) {
        return;
    }

    MonitorItem *item = MONITOR_ITEM(selected);

    AdwAlertDialog *dialog = ADW_ALERT_DIALOG(adw_alert_dialog_new("Rename Display", "Set a custom name for this display."));
    adw_alert_dialog_add_response(dialog, "cancel", "_Cancel");
    adw_alert_dialog_add_response(dialog, "rename", "_Rename");
    adw_alert_dialog_set_default_response(dialog, "rename");
    adw_alert_dialog_set_close_response(dialog, "cancel");
    adw_alert_dialog_set_response_appearance(dialog, "rename", ADW_RESPONSE_SUGGESTED);

    GtkWidget *entry = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(entry), monitor_item_get_name(item));
    gtk_widget_set_hexpand(entry, TRUE);
    adw_alert_dialog_set_extra_child(dialog, entry);

    RenameDialogData *data = g_new0(RenameDialogData, 1);
    data->window = self;
    data->item = g_object_ref(item);
    g_object_set_data_full(G_OBJECT(dialog), "rename-data", data, (GDestroyNotify)rename_dialog_data_free);

    g_signal_connect(entry, "activate", G_CALLBACK(on_rename_entry_activated), dialog);

    adw_alert_dialog_choose(dialog, GTK_WIDGET(self->window), NULL, on_rename_dialog_chosen, data);
    gtk_widget_grab_focus(entry);
}

static void on_list_item_setup(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data) {
    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    gtk_widget_add_css_class(row, "navigation-sidebar-item");
    gtk_widget_set_margin_top(row, 12);
    gtk_widget_set_margin_bottom(row, 12);
    gtk_widget_set_margin_start(row, 18);
    gtk_widget_set_margin_end(row, 18);

    GtkWidget *title = gtk_label_new("");
    gtk_widget_add_css_class(title, "title-4");
    gtk_label_set_xalign(GTK_LABEL(title), 0.0f);
    gtk_box_append(GTK_BOX(row), title);

    GtkWidget *subtitle = gtk_label_new("");
    gtk_widget_add_css_class(subtitle, "dim-label");
    gtk_label_set_xalign(GTK_LABEL(subtitle), 0.0f);
    gtk_box_append(GTK_BOX(row), subtitle);

    g_object_set_data(G_OBJECT(row), "title", title);
    g_object_set_data(G_OBJECT(row), "subtitle", subtitle);

    gtk_list_item_set_child(item, row);
}

static void on_list_item_bind(GtkSignalListItemFactory *factory, GtkListItem *item, gpointer user_data) {
    GtkWidget *row = gtk_list_item_get_child(item);
    GtkLabel *title = GTK_LABEL(g_object_get_data(G_OBJECT(row), "title"));
    GtkLabel *subtitle = GTK_LABEL(g_object_get_data(G_OBJECT(row), "subtitle"));
    MonitorItem *monitor = MONITOR_ITEM(gtk_list_item_get_item(item));

    gtk_label_set_text(title, monitor_item_get_name(monitor));

    const gchar *bus = monitor_item_get_bus(monitor);
    if (bus && *bus) {
        gtk_label_set_text(subtitle, bus);
        gtk_widget_set_visible(GTK_WIDGET(subtitle), TRUE);
    } else {
        gtk_widget_set_visible(GTK_WIDGET(subtitle), FALSE);
    }
}

static GtkWidget *build_list_view(AppWindow *self) {
    GtkListItemFactory *factory = gtk_signal_list_item_factory_new();
    g_signal_connect(factory, "setup", G_CALLBACK(on_list_item_setup), NULL);
    g_signal_connect(factory, "bind", G_CALLBACK(on_list_item_bind), NULL);

    GtkListView *list_view = GTK_LIST_VIEW(gtk_list_view_new(GTK_SELECTION_MODEL(self->selection), factory));
    gtk_list_view_set_single_click_activate(list_view, TRUE);
    gtk_list_view_set_show_separators(list_view, TRUE);
    gtk_widget_add_css_class(GTK_WIDGET(list_view), "navigation-sidebar");

    GtkWidget *scrolled = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scrolled), GTK_WIDGET(list_view));
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(scrolled), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
    return scrolled;
}

static GtkWidget *build_sidebar(AppWindow *self) {
    GtkWidget *sidebar = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_widget_add_css_class(sidebar, "navigation-sidebar");
    gtk_widget_add_css_class(sidebar, "gnomeddc-sidebar");

    GtkWidget *title_wrapper = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(title_wrapper, 18);
    gtk_widget_set_margin_bottom(title_wrapper, 18);
    gtk_widget_set_margin_start(title_wrapper, 24);
    gtk_widget_set_margin_end(title_wrapper, 24);

    GtkWidget *app_title = gtk_label_new("GnomeDDC");
    gtk_widget_add_css_class(app_title, "title-3");
    gtk_widget_add_css_class(app_title, "gnomeddc-sidebar-title");
    gtk_widget_set_halign(app_title, GTK_ALIGN_CENTER);
    gtk_widget_set_valign(app_title, GTK_ALIGN_CENTER);
    gtk_box_append(GTK_BOX(title_wrapper), app_title);

    gtk_box_append(GTK_BOX(sidebar), title_wrapper);

    GtkWidget *list = build_list_view(self);
    gtk_widget_set_vexpand(list, TRUE);
    gtk_box_append(GTK_BOX(sidebar), list);

    return sidebar;
}

static GtkWidget *build_detail_panel(AppWindow *self) {
    GtkWidget *detail_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 24);
    gtk_widget_set_margin_top(detail_box, 24);
    gtk_widget_set_margin_bottom(detail_box, 24);
    gtk_widget_set_margin_start(detail_box, 24);
    gtk_widget_set_margin_end(detail_box, 24);

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    GtkWidget *title_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_widget_set_halign(title_row, GTK_ALIGN_FILL);
    gtk_widget_set_hexpand(title_row, TRUE);

    self->title_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->title_label), "title-2");
    gtk_label_set_xalign(self->title_label, 0.0f);
    gtk_widget_set_hexpand(GTK_WIDGET(self->title_label), TRUE);
    gtk_box_append(GTK_BOX(title_row), GTK_WIDGET(self->title_label));

    self->rename_button = GTK_BUTTON(gtk_button_new_from_icon_name("document-edit-symbolic"));
    gtk_widget_add_css_class(GTK_WIDGET(self->rename_button), "flat");
    gtk_widget_add_css_class(GTK_WIDGET(self->rename_button), "circular");
    gtk_widget_set_valign(GTK_WIDGET(self->rename_button), GTK_ALIGN_CENTER);
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->rename_button), "Rename display");
    gtk_widget_set_sensitive(GTK_WIDGET(self->rename_button), FALSE);
    g_signal_connect(self->rename_button, "clicked", G_CALLBACK(on_rename_button_clicked), self);
    gtk_box_append(GTK_BOX(title_row), GTK_WIDGET(self->rename_button));

    gtk_box_append(GTK_BOX(title_box), title_row);

    self->subtitle_label = GTK_LABEL(gtk_label_new(""));
    gtk_widget_add_css_class(GTK_WIDGET(self->subtitle_label), "dim-label");
    gtk_label_set_xalign(self->subtitle_label, 0.0f);
    gtk_box_append(GTK_BOX(title_box), GTK_WIDGET(self->subtitle_label));
    gtk_box_append(GTK_BOX(detail_box), title_box);

    self->picture_size_group = gtk_size_group_new(GTK_SIZE_GROUP_HORIZONTAL);

    GtkAdjustment *adjustment = gtk_adjustment_new(0, 0, 100, 1, 5, 0);
    self->brightness_scale = GTK_SCALE(gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, adjustment));
    gtk_scale_set_draw_value(self->brightness_scale, FALSE);
    gtk_scale_set_digits(self->brightness_scale, 0);
    gtk_widget_set_hexpand(GTK_WIDGET(self->brightness_scale), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(self->brightness_scale), GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(self->picture_size_group, GTK_WIDGET(self->brightness_scale));

    self->brightness_value_label = GTK_LABEL(gtk_label_new("0%"));
    gtk_widget_add_css_class(GTK_WIDGET(self->brightness_value_label), "dim-label");
    gtk_widget_set_valign(GTK_WIDGET(self->brightness_value_label), GTK_ALIGN_CENTER);
    gtk_widget_set_halign(GTK_WIDGET(self->brightness_value_label), GTK_ALIGN_END);
    gtk_label_set_xalign(self->brightness_value_label, 1.0f);
    gtk_label_set_width_chars(self->brightness_value_label, 4);

    GtkWidget *brightness_suffix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(brightness_suffix, TRUE);
    gtk_widget_set_halign(brightness_suffix, GTK_ALIGN_FILL);

    gtk_box_append(GTK_BOX(brightness_suffix), GTK_WIDGET(self->brightness_scale));
    gtk_box_append(GTK_BOX(brightness_suffix), GTK_WIDGET(self->brightness_value_label));

    AdwActionRow *brightness_row = ADW_ACTION_ROW(adw_action_row_new());
    gtk_widget_add_css_class(GTK_WIDGET(brightness_row), "flat");
    g_object_set(brightness_row, "activatable", FALSE, NULL);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(brightness_row), "Brightness");
    adw_action_row_add_suffix(brightness_row, brightness_suffix);
    gtk_widget_set_hexpand(GTK_WIDGET(brightness_row), TRUE);

    GtkWidget *controls_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(controls_group), "Picture");
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(controls_group), GTK_WIDGET(brightness_row));

    GtkAdjustment *contrast_adjustment = gtk_adjustment_new(0, 0, 100, 1, 5, 0);
    GtkScale *contrast_scale = GTK_SCALE(gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, contrast_adjustment));
    gtk_scale_set_draw_value(contrast_scale, FALSE);
    gtk_scale_set_digits(contrast_scale, 0);
    gtk_widget_set_hexpand(GTK_WIDGET(contrast_scale), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(contrast_scale), GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(self->picture_size_group, GTK_WIDGET(contrast_scale));

    GtkLabel *contrast_value = GTK_LABEL(gtk_label_new("0%"));
    gtk_widget_add_css_class(GTK_WIDGET(contrast_value), "dim-label");
    gtk_widget_set_valign(GTK_WIDGET(contrast_value), GTK_ALIGN_CENTER);
    gtk_widget_set_halign(GTK_WIDGET(contrast_value), GTK_ALIGN_END);
    gtk_label_set_width_chars(contrast_value, 4);

    GtkWidget *contrast_suffix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(contrast_suffix, TRUE);
    gtk_box_append(GTK_BOX(contrast_suffix), GTK_WIDGET(contrast_scale));
    gtk_box_append(GTK_BOX(contrast_suffix), GTK_WIDGET(contrast_value));

    AdwActionRow *contrast_row = ADW_ACTION_ROW(adw_action_row_new());
    gtk_widget_add_css_class(GTK_WIDGET(contrast_row), "flat");
    g_object_set(contrast_row, "activatable", FALSE, NULL);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(contrast_row), "Contrast");
    adw_action_row_add_suffix(contrast_row, contrast_suffix);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(controls_group), GTK_WIDGET(contrast_row));

    picture_control_init_binding(&self->contrast_control,
                                 self,
                                 contrast_scale,
                                 contrast_value,
                                 "12",
                                 "Contrast");

    gtk_box_append(GTK_BOX(detail_box), controls_group);

    GtkWidget *color_group = adw_preferences_group_new();
    adw_preferences_group_set_title(ADW_PREFERENCES_GROUP(color_group), "Color");

    GtkAdjustment *red_adjustment = gtk_adjustment_new(0, 0, 100, 1, 5, 0);
    GtkScale *red_scale = GTK_SCALE(gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, red_adjustment));
    gtk_scale_set_draw_value(red_scale, FALSE);
    gtk_scale_set_digits(red_scale, 0);
    gtk_widget_set_hexpand(GTK_WIDGET(red_scale), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(red_scale), GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(self->picture_size_group, GTK_WIDGET(red_scale));

    GtkLabel *red_value = GTK_LABEL(gtk_label_new("0%"));
    gtk_widget_add_css_class(GTK_WIDGET(red_value), "dim-label");
    gtk_widget_set_valign(GTK_WIDGET(red_value), GTK_ALIGN_CENTER);
    gtk_widget_set_halign(GTK_WIDGET(red_value), GTK_ALIGN_END);
    gtk_label_set_width_chars(red_value, 4);

    GtkWidget *red_suffix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(red_suffix, TRUE);
    gtk_box_append(GTK_BOX(red_suffix), GTK_WIDGET(red_scale));
    gtk_box_append(GTK_BOX(red_suffix), GTK_WIDGET(red_value));

    AdwActionRow *red_row = ADW_ACTION_ROW(adw_action_row_new());
    gtk_widget_add_css_class(GTK_WIDGET(red_row), "flat");
    g_object_set(red_row, "activatable", FALSE, NULL);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(red_row), "Red");
    adw_action_row_add_suffix(red_row, red_suffix);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(color_group), GTK_WIDGET(red_row));

    picture_control_init_binding(&self->red_control, self, red_scale, red_value, "16", "Red");

    GtkAdjustment *green_adjustment = gtk_adjustment_new(0, 0, 100, 1, 5, 0);
    GtkScale *green_scale = GTK_SCALE(gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, green_adjustment));
    gtk_scale_set_draw_value(green_scale, FALSE);
    gtk_scale_set_digits(green_scale, 0);
    gtk_widget_set_hexpand(GTK_WIDGET(green_scale), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(green_scale), GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(self->picture_size_group, GTK_WIDGET(green_scale));

    GtkLabel *green_value = GTK_LABEL(gtk_label_new("0%"));
    gtk_widget_add_css_class(GTK_WIDGET(green_value), "dim-label");
    gtk_widget_set_valign(GTK_WIDGET(green_value), GTK_ALIGN_CENTER);
    gtk_widget_set_halign(GTK_WIDGET(green_value), GTK_ALIGN_END);
    gtk_label_set_width_chars(green_value, 4);

    GtkWidget *green_suffix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(green_suffix, TRUE);
    gtk_box_append(GTK_BOX(green_suffix), GTK_WIDGET(green_scale));
    gtk_box_append(GTK_BOX(green_suffix), GTK_WIDGET(green_value));

    AdwActionRow *green_row = ADW_ACTION_ROW(adw_action_row_new());
    gtk_widget_add_css_class(GTK_WIDGET(green_row), "flat");
    g_object_set(green_row, "activatable", FALSE, NULL);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(green_row), "Green");
    adw_action_row_add_suffix(green_row, green_suffix);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(color_group), GTK_WIDGET(green_row));

    picture_control_init_binding(&self->green_control, self, green_scale, green_value, "18", "Green");

    GtkAdjustment *blue_adjustment = gtk_adjustment_new(0, 0, 100, 1, 5, 0);
    GtkScale *blue_scale = GTK_SCALE(gtk_scale_new(GTK_ORIENTATION_HORIZONTAL, blue_adjustment));
    gtk_scale_set_draw_value(blue_scale, FALSE);
    gtk_scale_set_digits(blue_scale, 0);
    gtk_widget_set_hexpand(GTK_WIDGET(blue_scale), TRUE);
    gtk_widget_set_valign(GTK_WIDGET(blue_scale), GTK_ALIGN_CENTER);
    gtk_size_group_add_widget(self->picture_size_group, GTK_WIDGET(blue_scale));

    GtkLabel *blue_value = GTK_LABEL(gtk_label_new("0%"));
    gtk_widget_add_css_class(GTK_WIDGET(blue_value), "dim-label");
    gtk_widget_set_valign(GTK_WIDGET(blue_value), GTK_ALIGN_CENTER);
    gtk_widget_set_halign(GTK_WIDGET(blue_value), GTK_ALIGN_END);
    gtk_label_set_width_chars(blue_value, 4);

    GtkWidget *blue_suffix = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    gtk_widget_set_hexpand(blue_suffix, TRUE);
    gtk_box_append(GTK_BOX(blue_suffix), GTK_WIDGET(blue_scale));
    gtk_box_append(GTK_BOX(blue_suffix), GTK_WIDGET(blue_value));

    AdwActionRow *blue_row = ADW_ACTION_ROW(adw_action_row_new());
    gtk_widget_add_css_class(GTK_WIDGET(blue_row), "flat");
    g_object_set(blue_row, "activatable", FALSE, NULL);
    adw_preferences_row_set_title(ADW_PREFERENCES_ROW(blue_row), "Blue");
    adw_action_row_add_suffix(blue_row, blue_suffix);
    adw_preferences_group_add(ADW_PREFERENCES_GROUP(color_group), GTK_WIDGET(blue_row));

    picture_control_init_binding(&self->blue_control, self, blue_scale, blue_value, "1A", "Blue");

    gtk_box_append(GTK_BOX(detail_box), color_group);

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

    g_signal_connect(self->brightness_scale, "value-changed", G_CALLBACK(on_brightness_value_changed), self);

    return detail_box;
}

static void app_window_show_status(AppWindow *self, const gchar *icon, const gchar *title, const gchar *description) {
    adw_status_page_set_icon_name(self->status_page, icon);
    adw_status_page_set_title(self->status_page, title);
    adw_status_page_set_description(self->status_page, description);
    gtk_stack_set_visible_child_name(self->content_stack, "status");
    if (self->rename_button) {
        gtk_widget_set_sensitive(GTK_WIDGET(self->rename_button), FALSE);
    }
}

static void app_window_show_monitor(AppWindow *self, MonitorItem *item) {
    gtk_stack_set_visible_child_name(self->content_stack, "detail");
    app_window_set_feedback(self, "");

    gtk_label_set_text(self->title_label, monitor_item_get_name(item));
    gchar *subtitle = g_strdup_printf("Display %s", monitor_item_get_display_id(item));
    gtk_label_set_text(self->subtitle_label, subtitle);
    g_free(subtitle);

    if (self->rename_button) {
        gtk_widget_set_sensitive(GTK_WIDGET(self->rename_button), TRUE);
    }

    const gchar *bus = monitor_item_get_bus(item);
    adw_action_row_set_subtitle(self->bus_row, (bus && *bus) ? bus : "Not reported");

    const gchar *serial = monitor_item_get_serial(item);
    adw_action_row_set_subtitle(self->serial_row, (serial && *serial) ? serial : "Not reported");

    GError *error = NULL;
    gint current = 0;
    gint maximum = 100;
    if (!ddcutil_get_brightness(monitor_item_get_display_id(item), &current, &maximum, &error)) {
        app_window_set_feedback(self, error ? error->message : "Unable to query brightness");
        if (error) {
            g_error_free(error);
        }
        return;
    }

    self->updating_slider = TRUE;
    self->brightness_max = maximum > 0 ? maximum : 100;
    gtk_range_set_range(GTK_RANGE(self->brightness_scale), 0, self->brightness_max);
    gtk_range_set_value(GTK_RANGE(self->brightness_scale), current);
    self->updating_slider = FALSE;
    app_window_update_brightness_label(self, current);

    const gchar *display_id = monitor_item_get_display_id(item);
    picture_control_load_value(&self->contrast_control, display_id);
    picture_control_load_value(&self->red_control, display_id);
    picture_control_load_value(&self->green_control, display_id);
    picture_control_load_value(&self->blue_control, display_id);
}

static void app_window_refresh(AppWindow *self) {
    gtk_widget_set_sensitive(GTK_WIDGET(self->refresh_button), FALSE);
    app_window_show_status(self, "view-refresh-symbolic", "Detecting displays", "Querying ddcutil for connected monitors…");

    g_list_store_remove_all(self->monitor_store);

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

    gtk_single_selection_set_selected(self->selection, 0);
}

static AppWindow *app_window_new(GtkApplication *app) {
    AppWindow *self = g_new0(AppWindow, 1);

    self->window = ADW_APPLICATION_WINDOW(adw_application_window_new(app));
    gtk_window_set_title(GTK_WINDOW(self->window), "GnomeDDC");
    gtk_window_set_default_size(GTK_WINDOW(self->window), 900, 600);

    self->monitor_store = g_list_store_new(MONITOR_TYPE_ITEM);
    self->selection = GTK_SINGLE_SELECTION(gtk_single_selection_new(G_LIST_MODEL(self->monitor_store)));
    gtk_single_selection_set_autoselect(self->selection, FALSE);
    gtk_single_selection_set_can_unselect(self->selection, FALSE);
    g_signal_connect(self->selection, "notify::selected-item", G_CALLBACK(on_selection_changed), self);
    self->brightness_max = 100;

    ensure_app_styles();
    gtk_widget_add_css_class(GTK_WIDGET(self->window), "gnomeddc-window");

    GtkWidget *toolbar_view = adw_toolbar_view_new();
    GtkWidget *header_bar = adw_header_bar_new();
    gtk_widget_add_css_class(header_bar, "flat");

    GtkWidget *title_box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_widget_set_hexpand(title_box, TRUE);

    GtkWidget *page_title = gtk_label_new("GnomeDDC");
    gtk_widget_add_css_class(page_title, "title-6");
    gtk_widget_add_css_class(page_title, "gnomeddc-page-title");
    gtk_widget_set_halign(page_title, GTK_ALIGN_END);
    gtk_widget_set_margin_end(page_title, 12);
    gtk_box_append(GTK_BOX(title_box), page_title);

    gtk_widget_add_css_class(header_bar, "gnomeddc-header-bar");
    adw_header_bar_set_title_widget(ADW_HEADER_BAR(header_bar), title_box);

    self->refresh_button = GTK_BUTTON(gtk_button_new_from_icon_name("view-refresh-symbolic"));
    gtk_widget_set_tooltip_text(GTK_WIDGET(self->refresh_button), "Re-detect connected monitors");
    adw_header_bar_pack_end(ADW_HEADER_BAR(header_bar), GTK_WIDGET(self->refresh_button));
    g_signal_connect(self->refresh_button, "clicked", G_CALLBACK(on_refresh_clicked), self);

    gtk_widget_add_css_class(toolbar_view, "gnomeddc-toolbar-view");
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar_view), header_bar);

    GtkWidget *split_view = adw_navigation_split_view_new();
    gtk_widget_add_css_class(split_view, "gnomeddc-split-view");
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

    AdwNavigationPage *content_page = adw_navigation_page_new(GTK_WIDGET(self->content_stack), "Details");
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

static void ensure_app_styles(void) {
    static gsize initialized = 0;
    if (g_once_init_enter(&initialized)) {
        const gchar *css =
            ".gnomeddc-window .gnomeddc-sidebar {"
            "  background-color: @sidebar_bg_color;"
            "}"
            ".gnomeddc-window .gnomeddc-sidebar-title {"
            "  font-weight: 600;"
            "}"
            ".gnomeddc-window .gnomeddc-toolbar-view {"
            "  background-color: transparent;"
            "}"
            ".gnomeddc-window .gnomeddc-header-bar {"
            "  background-color: transparent;"
            "  box-shadow: none;"
            "}"
            ".gnomeddc-window .gnomeddc-page-title {"
            "  font-weight: 600;"
            "}";

        GtkCssProvider *provider = gtk_css_provider_new();
        gtk_css_provider_load_from_string(provider, css);

        GdkDisplay *display = gdk_display_get_default();
        if (display) {
            gtk_style_context_add_provider_for_display(display, GTK_STYLE_PROVIDER(provider), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        }
        g_object_unref(provider);
        g_once_init_leave(&initialized, 1);
    }
}

int main(int argc, char *argv[]) {
    g_autoptr(AdwApplication) app = adw_application_new("dev.gnomeddc", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), NULL);
    return g_application_run(G_APPLICATION(app), argc, argv);
}
