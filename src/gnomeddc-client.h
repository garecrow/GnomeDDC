#ifndef GNOMEDDC_CLIENT_H
#define GNOMEDDC_CLIENT_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define GNOMEDDC_TYPE_CLIENT (gnomeddc_client_get_type())

typedef struct _GnomeDdcClient GnomeDdcClient;

typedef enum {
  GNOMEDDC_CLIENT_BUS_SYSTEM,
  GNOMEDDC_CLIENT_BUS_SESSION
} GnomeDdcClientBusType;

G_DECLARE_FINAL_TYPE(GnomeDdcClient, gnomeddc_client, GNOMEDDC, CLIENT, GObject)

GnomeDdcClient *gnomeddc_client_new(void);

GDBusProxy *gnomeddc_client_get_proxy(GnomeDdcClient *self);
gboolean gnomeddc_client_is_connected(GnomeDdcClient *self);
GnomeDdcClientBusType gnomeddc_client_get_bus_type(GnomeDdcClient *self);
const gchar *gnomeddc_client_get_last_error(GnomeDdcClient *self);

void gnomeddc_client_call_async(GnomeDdcClient *self,
                                const gchar *method,
                                GVariant *parameters,
                                GCancellable *cancellable,
                                GAsyncReadyCallback callback,
                                gpointer user_data);

GVariant *gnomeddc_client_call_finish(GnomeDdcClient *self,
                                      GAsyncResult *result,
                                      GError **error);

GVariant *gnomeddc_client_get_cached_property(GnomeDdcClient *self,
                                              const gchar *property_name);

void gnomeddc_client_set_property_async(GnomeDdcClient *self,
                                        const gchar *property_name,
                                        GVariant *value,
                                        GCancellable *cancellable,
                                        GAsyncReadyCallback callback,
                                        gpointer user_data);

GVariant *gnomeddc_client_set_property_finish(GnomeDdcClient *self,
                                              GAsyncResult *result,
                                              GError **error);

G_END_DECLS

#endif /* GNOMEDDC_CLIENT_H */
