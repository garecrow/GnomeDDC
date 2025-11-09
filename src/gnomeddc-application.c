#include "gnomeddc-application.h"

#include "gnomeddc-window.h"

#include <gio/gio.h>

extern GResource *gnomeddc_get_resource(void);

struct _GnomeDdcApplication {
  AdwApplication parent_instance;
};

G_DEFINE_FINAL_TYPE(GnomeDdcApplication, gnome_ddc_application, ADW_TYPE_APPLICATION)

static void
ensure_resources_registered(void)
{
  static gsize registered = 0;

  if (g_once_init_enter(&registered)) {
    g_resources_register(gnomeddc_get_resource());
    g_once_init_leave(&registered, 1);
  }
}

static void
gnome_ddc_application_activate(GApplication *app)
{
  ensure_resources_registered();

  GList *windows = gtk_application_get_windows(GTK_APPLICATION(app));
  if (windows) {
    gtk_window_present(GTK_WINDOW(windows->data));
    return;
  }

  GnomeDdcWindow *window = g_object_new(GNOMEDDC_TYPE_WINDOW,
                                        "application", app,
                                        NULL);
  gtk_window_present(GTK_WINDOW(window));
}

static void
gnome_ddc_application_class_init(GnomeDdcApplicationClass *klass)
{
  GApplicationClass *app_class = G_APPLICATION_CLASS(klass);
  app_class->activate = gnome_ddc_application_activate;
}

static void
gnome_ddc_application_init(GnomeDdcApplication *self)
{
  g_application_set_resource_base_path(G_APPLICATION(self), "/com/ddcutil/GnomeDDC");
}

GnomeDdcApplication *
gnome_ddc_application_new(void)
{
  return g_object_new(GNOMEDDC_TYPE_APPLICATION,
                      "application-id", "com.ddcutil.GnomeDDC",
                      "flags", G_APPLICATION_HANDLES_OPEN,
                      NULL);
}
