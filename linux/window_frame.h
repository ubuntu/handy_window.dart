#ifndef HANDY_WINDOW_WINDOW_FRAME_H_
#define HANDY_WINDOW_WINDOW_FRAME_H_

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

// No visible change until the app calls enableWindowFrame() in Dart.
void window_frame_attach(GtkWindow* window, FlView* view);

#endif  // HANDY_WINDOW_WINDOW_FRAME_H_
