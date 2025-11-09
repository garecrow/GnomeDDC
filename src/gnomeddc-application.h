#ifndef GNOMEDDC_APPLICATION_H
#define GNOMEDDC_APPLICATION_H

#include <adwaita.h>

G_BEGIN_DECLS

#define GNOMEDDC_TYPE_APPLICATION (gnome_ddc_application_get_type())

G_DECLARE_FINAL_TYPE(GnomeDdcApplication, gnome_ddc_application, GNOMEDDC, APPLICATION, AdwApplication)

GnomeDdcApplication *gnome_ddc_application_new(void);

G_END_DECLS

#endif /* GNOMEDDC_APPLICATION_H */
