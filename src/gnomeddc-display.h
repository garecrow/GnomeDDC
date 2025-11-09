#ifndef GNOMEDDC_DISPLAY_H
#define GNOMEDDC_DISPLAY_H

#include <glib-object.h>

G_BEGIN_DECLS

#define GNOMEDDC_TYPE_DISPLAY (gnomeddc_display_get_type())

G_DECLARE_FINAL_TYPE(GnomeDdcDisplay, gnomeddc_display, GNOMEDDC, DISPLAY, GObject)

GnomeDdcDisplay *gnomeddc_display_new(gint display_number,
                                      gint usb_bus,
                                      gint usb_device,
                                      const gchar *manufacturer,
                                      const gchar *model,
                                      const gchar *serial,
                                      guint16 product_code,
                                      const gchar *edid,
                                      guint32 binary_serial);

gint gnomeddc_display_get_display_number(GnomeDdcDisplay *self);
gint gnomeddc_display_get_usb_bus(GnomeDdcDisplay *self);
gint gnomeddc_display_get_usb_device(GnomeDdcDisplay *self);
const gchar *gnomeddc_display_get_manufacturer(GnomeDdcDisplay *self);
const gchar *gnomeddc_display_get_model(GnomeDdcDisplay *self);
const gchar *gnomeddc_display_get_serial(GnomeDdcDisplay *self);
guint16 gnomeddc_display_get_product_code(GnomeDdcDisplay *self);
const gchar *gnomeddc_display_get_edid(GnomeDdcDisplay *self);
guint32 gnomeddc_display_get_binary_serial(GnomeDdcDisplay *self);
char *gnomeddc_display_dup_full_name(GnomeDdcDisplay *self);

G_END_DECLS

#endif /* GNOMEDDC_DISPLAY_H */
