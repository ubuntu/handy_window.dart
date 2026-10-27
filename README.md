# Handy Window

[![pub](https://img.shields.io/pub/v/handy_window.svg)](https://pub.dev/packages/handy_window)

This package enhances the looks of Flutter applications on Linux by providing
modern-looking [Handy](https://gitlab.gnome.org/GNOME/libhandy) windows with
rounded bottom corners.

| Handy window | Flutter window |
|---|---|
| <image src="https://raw.githubusercontent.com/ubuntu-flutter-community/handy_window/main/images/handy-window.png" width="430"/> | <image src="https://raw.githubusercontent.com/ubuntu-flutter-community/handy_window/main/images/flutter-window.png" width="400"/> |

**NOTE:** Handy windows are only available on Ubuntu 20.04 (GLib >= 2.58) and later.

## Usage

Add the dependency to `pubspec.yaml`:
```yaml
dependencies:
  handy_window:
```

Modify the native code to register plugins before showing the Flutter window.

### For Flutter new versions

In `linux/runner/my_application.cc`, move `fl_register_plugins` before `gtk_widget_show(GTK_WIDGET(view))`:

```diff
   // for transparent.
   gdk_rgba_parse(&background_color, "#000000");
   fl_view_set_background_color(view, &background_color);
-  gtk_widget_show(GTK_WIDGET(view));
   gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
 
+  fl_register_plugins(FL_PLUGIN_REGISTRY(view));
+  gtk_widget_show(GTK_WIDGET(view));

   // Show the window when Flutter renders.
   // Requires the view to be realized so we can start rendering.
   g_signal_connect_swapped(view, "first-frame", G_CALLBACK(first_frame_cb),
                            self);
   gtk_widget_realize(GTK_WIDGET(view));
 
-  fl_register_plugins(FL_PLUGIN_REGISTRY(view));
-
   gtk_widget_grab_focus(GTK_WIDGET(view));
 }
```

### For Flutter old versions

In `linux/my_application.cc`, register plugins before showing the window and view:

```diff
diff --git a/linux/my_application.cc b/linux/my_application.cc
index fa74baa..3133755 100644
--- a/linux/my_application.cc
+++ b/linux/my_application.cc
@@ -48,17 +48,17 @@ static void my_application_activate(GApplication* application) {
   }

   gtk_window_set_default_size(window, 1280, 720);
-  gtk_widget_show(GTK_WIDGET(window));

   g_autoptr(FlDartProject) project = fl_dart_project_new();
   fl_dart_project_set_dart_entrypoint_arguments(project, self->dart_entrypoint_arguments);

   FlView* view = fl_view_new(project);
-  gtk_widget_show(GTK_WIDGET(view));
   gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));

   fl_register_plugins(FL_PLUGIN_REGISTRY(view));

+  gtk_widget_show(GTK_WIDGET(window));
+  gtk_widget_show(GTK_WIDGET(view));
   gtk_widget_grab_focus(GTK_WIDGET(view));
 }
```

## Faster rounded corners

Under themes that round the window's corners, such as Yaru, libhandy masks them
by drawing the whole window offscreen, which makes GTK read every Flutter frame
back from the GPU and makes scrolling and animations stutter. To have Flutter
paint the same corners instead, call `enableWindowFrame()` before `runApp()`
and wrap the app in a `WindowFrame`:

```dart
Future<void> main() async {
  WidgetsFlutterBinding.ensureInitialized();
  final windowFrame = await enableWindowFrame();
  runApp(WindowFrame(style: windowFrame, child: const MyApp()));
}
```

The window looks the same either way, except that transparent parts of the app
show the window's background. Flutter can only paint the corners when its view
fills the window, as in apps that draw their own title bar, for example with
`YaruWindowTitleBar` from the yaru package. Elsewhere, such as under a GTK
header bar, on X11, or with GTK 3.24.39 to 3.24.41, which can't blend Flutter's
corners over the window shadow, libhandy keeps painting them.
