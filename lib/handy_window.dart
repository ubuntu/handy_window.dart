/// # Handy Window
///
/// This package enhances the looks of Flutter applications on Linux by providing
/// modern-looking [Handy](https://gitlab.gnome.org/GNOME/libhandy) windows with
/// rounded bottom corners.
///
/// | Handy window | Flutter window |
/// |---|---|
/// | <img src="https://raw.githubusercontent.com/ubuntu-flutter-community/handy_window/main/images/handy-window.png" width="430"/> | <img src="https://raw.githubusercontent.com/ubuntu-flutter-community/handy_window/main/images/flutter-window.png" width="400"/> |
///
/// ## Usage
///
/// Add the dependency to `pubspec.yaml`:
/// ```yaml
/// dependencies:
///   handy_window:
/// ```
///
/// Modify `linux/my_application.cc` to register plugins before showing the Flutter
/// window and view:
///
/// ```diff
/// diff --git a/linux/my_application.cc b/linux/my_application.cc
/// index fa74baa..3133755 100644
/// --- a/linux/my_application.cc
/// +++ b/linux/my_application.cc
/// @@ -48,17 +48,17 @@ static void my_application_activate(GApplication* application) {
///    }
///
///    gtk_window_set_default_size(window, 1280, 720);
/// -  gtk_widget_show(GTK_WIDGET(window));
///
///    g_autoptr(FlDartProject) project = fl_dart_project_new();
///    fl_dart_project_set_dart_entrypoint_arguments(project, self->dart_entrypoint_arguments);
///
///    FlView* view = fl_view_new(project);
/// -  gtk_widget_show(GTK_WIDGET(view));
///    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
///
///    fl_register_plugins(FL_PLUGIN_REGISTRY(view));
///
/// +  gtk_widget_show(GTK_WIDGET(window));
/// +  gtk_widget_show(GTK_WIDGET(view));
///    gtk_widget_grab_focus(GTK_WIDGET(view));
///  }
/// ```
///
/// ## Faster rounded corners
///
/// Under themes that round the window's corners, such as Yaru, libhandy masks
/// them by drawing the whole window offscreen, which makes GTK read every
/// Flutter frame back from the GPU and makes scrolling and animations stutter.
/// To have Flutter paint the same corners instead, call [enableWindowFrame]
/// before `runApp()` and wrap the app in a [WindowFrame]:
///
/// ```dart
/// Future<void> main() async {
///   WidgetsFlutterBinding.ensureInitialized();
///   final windowFrame = await enableWindowFrame();
///   runApp(WindowFrame(style: windowFrame, child: const MyApp()));
/// }
/// ```
///
/// The window looks the same either way, except that transparent parts of the
/// app show the window's background. Flutter can only paint the corners when
/// its view fills the window, as in apps that draw their own title bar, for
/// example with `YaruWindowTitleBar` from the yaru package. Elsewhere, such as
/// under a GTK header bar or on X11, libhandy keeps painting them.
library;

export 'src/window_frame.dart'
    show WindowFrame, WindowFrameStyle, enableWindowFrame;
