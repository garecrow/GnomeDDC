#include "gnomeddc-client.h"

#define DDCUTIL_SERVICE_NAME "com.ddcutil.DdcutilService"
#define DDCUTIL_OBJECT_PATH "/com/ddcutil/DdcutilObject"
#define DDCUTIL_INTERFACE_NAME "com.ddcutil.DdcutilInterface"

struct _GnomeDdcClient {
  GObject parent_instance;

  GDBusProxy *proxy;
  GnomeDdcClientBusType bus_type;
  gchar *last_error;
};

G_DEFINE_FINAL_TYPE(GnomeDdcClient, gnomeddc_client, G_TYPE_OBJECT)

static void
set_last_error(GnomeDdcClient *self, const gchar *message)
{
  g_clear_pointer(&self->last_error, g_free);
  if (message != NULL) {
    self->last_error = g_strdup(message);
  }
}

static void
gnomeddc_client_constructed(GObject *object)
{
  G_OBJECT_CLASS(gnomeddc_client_parent_class)->constructed(object);

  GnomeDdcClient *self = GNOMEDDC_CLIENT(object);
  g_autoptr(GError) error = NULL;
  GDBusConnection *connection = g_bus_get_sync(G_BUS_TYPE_SYSTEM, NULL, &error);

  if (connection == NULL) {
    set_last_error(self, error != NULL ? error->message : "");
    g_clear_error(&error);
    connection = g_bus_get_sync(G_BUS_TYPE_SESSION, NULL, &error);
    self->bus_type = GNOMEDDC_CLIENT_BUS_SESSION;
  } else {
    self->bus_type = GNOMEDDC_CLIENT_BUS_SYSTEM;
  }

  if (connection == NULL) {
    set_last_error(self, error != NULL ? error->message : "Failed to connect to D-Bus");
    g_warning("Unable to connect to D-Bus: %s", self->last_error ? self->last_error : "unknown error");
    return;
  }

  g_autoptr(GError) proxy_error = NULL;
  self->proxy = g_dbus_proxy_new_sync(connection,
                                      G_DBUS_PROXY_FLAGS_NONE,
                                      NULL,
                                      DDCUTIL_SERVICE_NAME,
                                      DDCUTIL_OBJECT_PATH,
                                      DDCUTIL_INTERFACE_NAME,
                                      NULL,
                                      &proxy_error);
  g_object_unref(connection);

  if (self->proxy == NULL) {
    set_last_error(self, proxy_error != NULL ? proxy_error->message : "Unable to create D-Bus proxy");
    g_warning("Unable to create proxy for %s: %s", DDCUTIL_SERVICE_NAME, self->last_error ? self->last_error : "");
    return;
  }

  set_last_error(self, NULL);
}

static void
gnomeddc_client_finalize(GObject *object)
{
  GnomeDdcClient *self = GNOMEDDC_CLIENT(object);
  g_clear_object(&self->proxy);
  g_clear_pointer(&self->last_error, g_free);
  G_OBJECT_CLASS(gnomeddc_client_parent_class)->finalize(object);
}

static void
gnomeddc_client_class_init(GnomeDdcClientClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->constructed = gnomeddc_client_constructed;
  object_class->finalize = gnomeddc_client_finalize;
}

static void
gnomeddc_client_init(GnomeDdcClient *self)
{
  self->bus_type = GNOMEDDC_CLIENT_BUS_SYSTEM;
}

GnomeDdcClient *
gnomeddc_client_new(void)
{
  return g_object_new(GNOMEDDC_TYPE_CLIENT, NULL);
}

GDBusProxy *
gnomeddc_client_get_proxy(GnomeDdcClient *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_CLIENT(self), NULL);
  return self->proxy;
}

gboolean
gnomeddc_client_is_connected(GnomeDdcClient *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_CLIENT(self), FALSE);
  return self->proxy != NULL;
}

GnomeDdcClientBusType
gnomeddc_client_get_bus_type(GnomeDdcClient *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_CLIENT(self), GNOMEDDC_CLIENT_BUS_SESSION);
  return self->bus_type;
}

const gchar *
gnomeddc_client_get_last_error(GnomeDdcClient *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_CLIENT(self), NULL);
  return self->last_error;
}

static GVariant *
ensure_parameters(GVariant *parameters)
{
  if (parameters != NULL) {
    return g_variant_ref_sink(parameters);
  }

  return g_variant_ref_sink(g_variant_new_tuple(NULL, 0));
}

void
gnomeddc_client_call_async(GnomeDdcClient *self,
                                const gchar *method,
                                GVariant *parameters,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data)
{
  g_return_if_fail(GNOMEDDC_IS_CLIENT(self));
  g_return_if_fail(method != NULL);

  if (self->proxy == NULL) {
    GTask *task = g_task_new(self, cancellable, callback, user_data);
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", self->last_error ? self->last_error : "Proxy unavailable");
    g_object_unref(task);
    return;
  }

  GVariant *params = ensure_parameters(parameters);
  g_dbus_proxy_call(self->proxy,
                    method,
                    params,
                    G_DBUS_CALL_FLAGS_NONE,
                    -1,
                    cancellable,
                    callback,
                    user_data);
  g_variant_unref(params);
}

GVariant *
gnomeddc_client_call_finish(GnomeDdcClient *self,
                                      GAsyncResult *result,
                                      GError **error)
{
  g_return_val_if_fail(GNOMEDDC_IS_CLIENT(self), NULL);
  g_return_val_if_fail(G_IS_ASYNC_RESULT(result), NULL);

  if (self->proxy == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", self->last_error ? self->last_error : "Proxy unavailable");
    return NULL;
  }

  return g_dbus_proxy_call_finish(self->proxy, result, error);
}

GVariant *
gnomeddc_client_get_cached_property(GnomeDdcClient *self,
                                              const gchar *property_name)
{
  g_return_val_if_fail(GNOMEDDC_IS_CLIENT(self), NULL);
  g_return_val_if_fail(property_name != NULL, NULL);

  if (self->proxy == NULL) {
    return NULL;
  }

  GVariant *value = g_dbus_proxy_get_cached_property(self->proxy, property_name);
  if (value != NULL) {
    return g_variant_ref(value);
  }
  return NULL;
}

void
gnomeddc_client_set_property_async(GnomeDdcClient *self,
                                        const gchar *property_name,
                                        GVariant *value,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data)
{
  g_return_if_fail(GNOMEDDC_IS_CLIENT(self));
  g_return_if_fail(property_name != NULL);

  if (self->proxy == NULL) {
    GTask *task = g_task_new(self, cancellable, callback, user_data);
    g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", self->last_error ? self->last_error : "Proxy unavailable");
    g_object_unref(task);
    return;
  }

  g_dbus_proxy_call(self->proxy,
                    "org.freedesktop.DBus.Properties.Set",
                    g_variant_new("(ssv)", DDCUTIL_INTERFACE_NAME, property_name, value),
                    G_DBUS_CALL_FLAGS_NONE,
                    -1,
                    cancellable,
                    callback,
                    user_data);
}

GVariant *
gnomeddc_client_set_property_finish(GnomeDdcClient *self,
                                              GAsyncResult *result,
                                              GError **error)
{
  g_return_val_if_fail(GNOMEDDC_IS_CLIENT(self), NULL);
  g_return_val_if_fail(G_IS_ASYNC_RESULT(result), NULL);

  if (self->proxy == NULL) {
    g_set_error(error, G_IO_ERROR, G_IO_ERROR_FAILED, "%s", self->last_error ? self->last_error : "Proxy unavailable");
    return NULL;
  }

  return g_dbus_proxy_call_finish(self->proxy, result, error);
}
