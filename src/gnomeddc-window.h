#ifndef GNOMEDDC_WINDOW_H
#define GNOMEDDC_WINDOW_H

#include <adwaita.h>

G_BEGIN_DECLS

#define GNOMEDDC_TYPE_WINDOW (gnomeddc_window_get_type())

G_DECLARE_FINAL_TYPE(GnomeDdcWindow, gnomeddc_window, GNOMEDDC, WINDOW, AdwApplicationWindow)

G_END_DECLS

#endif /* GNOMEDDC_WINDOW_H */
