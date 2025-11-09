#include "gnomeddc-display.h"

struct _GnomeDdcDisplay {
  GObject parent_instance;

  gint display_number;
  gint usb_bus;
  gint usb_device;
  gchar *manufacturer;
  gchar *model;
  gchar *serial;
  guint16 product_code;
  gchar *edid;
  guint32 binary_serial;
};

G_DEFINE_FINAL_TYPE(GnomeDdcDisplay, gnomeddc_display, G_TYPE_OBJECT)

static void
gnomeddc_display_finalize(GObject *object)
{
  GnomeDdcDisplay *self = GNOMEDDC_DISPLAY(object);

  g_clear_pointer(&self->manufacturer, g_free);
  g_clear_pointer(&self->model, g_free);
  g_clear_pointer(&self->serial, g_free);
  g_clear_pointer(&self->edid, g_free);

  G_OBJECT_CLASS(gnomeddc_display_parent_class)->finalize(object);
}

static void
gnomeddc_display_class_init(GnomeDdcDisplayClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS(klass);
  object_class->finalize = gnomeddc_display_finalize;
}

static void
gnomeddc_display_init(GnomeDdcDisplay *self)
{
  (void)self;
}

GnomeDdcDisplay *
gnomeddc_display_new(gint display_number,
                      gint usb_bus,
                      gint usb_device,
                      const gchar *manufacturer,
                      const gchar *model,
                      const gchar *serial,
                      guint16 product_code,
                      const gchar *edid,
                      guint32 binary_serial)
{
  GnomeDdcDisplay *self = g_object_new(GNOMEDDC_TYPE_DISPLAY, NULL);
  self->display_number = display_number;
  self->usb_bus = usb_bus;
  self->usb_device = usb_device;
  self->manufacturer = g_strdup(manufacturer ? manufacturer : "");
  self->model = g_strdup(model ? model : "");
  self->serial = g_strdup(serial ? serial : "");
  self->product_code = product_code;
  self->edid = g_strdup(edid ? edid : "");
  self->binary_serial = binary_serial;
  return self;
}

gint
gnomeddc_display_get_display_number(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), -1);
  return self->display_number;
}

gint
gnomeddc_display_get_usb_bus(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), -1);
  return self->usb_bus;
}

gint
gnomeddc_display_get_usb_device(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), -1);
  return self->usb_device;
}

const gchar *
gnomeddc_display_get_manufacturer(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), "");
  return self->manufacturer;
}

const gchar *
gnomeddc_display_get_model(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), "");
  return self->model;
}

const gchar *
gnomeddc_display_get_serial(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), "");
  return self->serial;
}

guint16
gnomeddc_display_get_product_code(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), 0);
  return self->product_code;
}

const gchar *
gnomeddc_display_get_edid(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), "");
  return self->edid;
}

guint32
gnomeddc_display_get_binary_serial(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), 0);
  return self->binary_serial;
}

char *
gnomeddc_display_dup_full_name(GnomeDdcDisplay *self)
{
  g_return_val_if_fail(GNOMEDDC_IS_DISPLAY(self), NULL);

  if (self->manufacturer[0] == '\0' && self->model[0] == '\0') {
    return g_strdup("Display");
  }

  if (self->manufacturer[0] == '\0') {
    return g_strdup(self->model);
  }

  if (self->model[0] == '\0') {
    return g_strdup(self->manufacturer);
  }

  return g_strdup_printf("%s %s", self->manufacturer, self->model);
}
