#include "include/handy_window/handy_window_plugin.h"

#ifdef HAVE_LIBHANDY

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>
#include <handy.h>

#include <functional>

#include "handy_settings.h"

#ifndef USE_LIBHANDY
#define _HANDY_INSIDE
#include "hdy-window-mixin-private.h"
#undef _HANDY_INSIDE

class HdyScopeGuard {
 public:
  HdyScopeGuard(std::function<void()>&& func) : func(std::move(func)) {}
  ~HdyScopeGuard() { func(); }
  std::function<void()> func;
};

#define IS_REENTRY(object) g_object_get_data(G_OBJECT(object), __func__)
#define GUARD_REENTRY(object)                                           \
  HdyScopeGuard guard(                                                  \
      [=] { g_object_set_data(G_OBJECT(object), __func__, nullptr); }); \
  g_object_set_data(G_OBJECT(object), __func__, gpointer(__func__))

static HdyWindowMixin* get_window_mixin(gpointer object) {
  return HDY_WINDOW_MIXIN(
      g_object_get_data(G_OBJECT(object), "hdy_window_mixin"));
}

static void set_window_mixin(gpointer object, HdyWindowMixin* mixin) {
  g_object_set_data(G_OBJECT(object), "hdy_window_mixin", mixin);
}

static void (*gtk_window_finalize)(GObject*) = nullptr;
static gboolean (*gtk_window_draw)(GtkWidget*, cairo_t*) = nullptr;
static void (*gtk_window_destroy)(GtkWidget*) = nullptr;
static void (*gtk_window_add)(GtkContainer*, GtkWidget*) = nullptr;
static void (*gtk_window_remove)(GtkContainer*, GtkWidget*) = nullptr;
static void (*gtk_window_forall)(GtkContainer*, gboolean, GtkCallback,
                                 gpointer) = nullptr;

static void hdy_window_add(GtkContainer* container, GtkWidget* widget) {
  if (IS_REENTRY(container)) {
    gtk_window_add(container, widget);
  } else {
    GUARD_REENTRY(container);
    hdy_window_mixin_add(get_window_mixin(container), widget);
  }
}

static void hdy_window_remove(GtkContainer* container, GtkWidget* widget) {
  if (IS_REENTRY(container)) {
    gtk_window_remove(container, widget);
  } else {
    GUARD_REENTRY(container);
    hdy_window_mixin_remove(get_window_mixin(container), widget);
  }
}

static void hdy_window_forall(GtkContainer* container,
                              gboolean include_internals, GtkCallback callback,
                              gpointer callback_data) {
  if (IS_REENTRY(container)) {
    gtk_window_forall(container, include_internals, callback, callback_data);
  } else {
    GUARD_REENTRY(container);
    hdy_window_mixin_forall(get_window_mixin(container), include_internals,
                            callback, callback_data);
  }
}

static gboolean hdy_window_draw(GtkWidget* widget, cairo_t* cr) {
  // Only bypass mixin when fullscreen/maximized to prevent recursion,
  // because hdy_window_mixin_draw calls original draw in these cases.
  GtkWindow* window = GTK_WINDOW(widget);
  if (gtk_window_is_maximized(window) ||
      (gdk_window_get_state(gtk_widget_get_window(widget)) &
       GDK_WINDOW_STATE_FULLSCREEN)) {
    return gtk_window_draw(widget, cr);
  }
  return hdy_window_mixin_draw(get_window_mixin(widget), cr);
}

static void hdy_window_destroy(GtkWidget* widget) {
  if (IS_REENTRY(widget)) {
    gtk_window_destroy(widget);
  } else {
    GUARD_REENTRY(widget);
    hdy_window_mixin_destroy(get_window_mixin(widget));
  }
}

static void hdy_window_finalize(GObject* object) {
  g_object_unref(G_OBJECT(get_window_mixin(object)));
  gtk_window_finalize(object);
}

static void copy_header_bar(HdyHeaderBar* hdy_header_bar,
                            GtkHeaderBar* gtk_header_bar) {
  hdy_header_bar_set_title(hdy_header_bar,
                           gtk_header_bar_get_title(gtk_header_bar));
  hdy_header_bar_set_show_close_button(
      hdy_header_bar, gtk_header_bar_get_show_close_button(gtk_header_bar));
  hdy_header_bar_set_decoration_layout(
      hdy_header_bar, gtk_header_bar_get_decoration_layout(gtk_header_bar));
}

static void update_header_bar_title(GtkWindow* window, GParamSpec*,
                                    gpointer user_data) {
  GtkWidget* header_bar = GTK_WIDGET(user_data);
  const gchar* title = gtk_window_get_title(window);
  hdy_header_bar_set_title(HDY_HEADER_BAR(header_bar), title);
}

static void update_header_bar_buttons(GtkWindow* window, GParamSpec*,
                                      gpointer user_data) {
  GtkWidget* header_bar = GTK_WIDGET(user_data);
  gboolean deletable = gtk_window_get_deletable(window);
  hdy_header_bar_set_show_close_button(HDY_HEADER_BAR(header_bar), deletable);
}

static void print_warning(const gchar* title, const gchar* before,
                          const gchar* after) {
  g_warning(
      "%s. In `linux/my_application.cc`, change `my_application_activate()` to "
      "call `%s` after calling `%s`.",
      title, after, before);
}

static gboolean sanity_check_window(GtkWidget* window) {
  if (!GTK_IS_WINDOW(window)) {
    print_warning("FlView must be added to GtkWindow",
                  "gtk_container_add(window, view)",
                  "fl_register_plugins(view)");
    return false;
  }
  if (gtk_widget_is_visible(window)) {
    print_warning("GtkWindow must be shown AFTER registering plugins",
                  "fl_register_plugins(view)", "gtk_widget_show(window)");
    return false;
  }
  return true;
}

static gboolean sanity_check_view(FlView* view) {
  if (gtk_widget_get_realized(GTK_WIDGET(view))) {
    print_warning("FlView must be realized AFTER registering plugins",
                  "fl_register_plugins(view)", "gtk_widget_realize(view)");
    return false;
  }
  return true;
}
#endif  // USE_LIBHANDY

static gboolean use_csd(GtkWidget* window) {
  // the screen must be composited and support alpha visuals for the shadow
  GdkScreen* screen = gtk_window_get_screen(GTK_WINDOW(window));
  if (!gdk_screen_is_composited(screen) ||
      !gdk_screen_get_rgba_visual(screen)) {
    return false;
  }

  // disable if GTK_CSD != 1
  const gchar* gtk_csd = g_getenv("GTK_CSD");
  if (gtk_csd != nullptr && g_strcmp0(gtk_csd, "1") != 0) {
    return false;
  }

  return true;
}

static void load_css(HandySettings* settings) {
  GdkScreen* screen = gdk_screen_get_default();
  gpointer css_provider =
      g_object_get_data(G_OBJECT(screen), "_handy_window_css_provider_");

  if (css_provider == nullptr) {
    css_provider = gtk_css_provider_new();
    gtk_style_context_add_provider_for_screen(
        screen, GTK_STYLE_PROVIDER(css_provider),
        GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_set_data_full(G_OBJECT(screen), "_handy_window_css_provider_",
                           css_provider, g_object_unref);
  }

  HandyColorScheme color_scheme = handy_settings_get_color_scheme(settings);

  g_autoptr(GError) error = nullptr;
  g_autofree gchar* exe_path = g_file_read_link("/proc/self/exe", &error);
  g_autofree gchar* app_path = g_path_get_dirname(exe_path);
  const gchar* asset_path = "data/flutter_assets/packages/handy_window/assets";
  const gchar* css_file = color_scheme == HANDY_COLOR_SCHEME_DARK
                              ? "handy-window-dark.css"
                              : "handy-window.css";
  g_autofree gchar* css_path =
      g_build_filename(app_path, asset_path, css_file, nullptr);
  if (!gtk_css_provider_load_from_path(GTK_CSS_PROVIDER(css_provider), css_path,
                                       &error)) {
    g_warning("%s: %s", css_path, error->message);
  }
}

// Flutter draws the window corners and outline (HandyWindowCorners), so that
// libhandy does not mask the corners by copying every frame from the GPU.
struct HandyCorners {
  FlView* view;
  GtkWidget* window;
  FlMethodChannel* channel;
  gboolean (*window_draw)(GtkWidget*, cairo_t*);
  // The corners Dart last heard about; radius -1 while Dart does not listen.
  gint radius;
  gboolean outline;
  // The radius Flutter has drawn; -1 while it draws none.
  gint drawn_radius;
};

// The corners libhandy draws, see hdy_window_mixin_draw().
static void get_corners(GtkWidget* window, gint* radius, gboolean* outline) {
  GtkStyleContext* context = gtk_widget_get_style_context(window);
  GdkWindow* gdk_window = gtk_widget_get_window(window);
  gboolean framed =
      gtk_window_get_decorated(GTK_WINDOW(window)) &&
      !gtk_style_context_has_class(context, "solid-csd") &&
      !gtk_window_is_maximized(GTK_WINDOW(window)) &&
      !(gdk_window != nullptr &&
        (gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_FULLSCREEN));
  *radius = 0;
  if (framed) {
    gtk_style_context_get(context, gtk_style_context_get_state(context),
                          GTK_STYLE_PROPERTY_BORDER_RADIUS, radius, nullptr);
  }
  // `window.csd.unified decoration-overlay` in assets/handy-window.scss. It
  // only shows when libhandy masks the corners: otherwise Flutter's GL frame
  // covers it.
  *outline = *radius > 0 && gtk_style_context_has_class(context, "csd") &&
             gtk_style_context_has_class(context, "unified");
}

static FlValue* corners_value(gint radius, gboolean outline) {
  FlValue* value = fl_value_new_map();
  fl_value_set_string_take(value, "radius", fl_value_new_int(radius));
  fl_value_set_string_take(value, "outline", fl_value_new_bool(outline));
  return value;
}

// Runs before the class handler, where libhandy masks the corners.
static gboolean corners_draw_cb(GtkWidget* window, cairo_t* cr,
                                HandyCorners* corners) {
  if (corners->radius < 0) {
    return FALSE;
  }

  // Read where libhandy reads them, so that Dart hears about every change.
  gint radius;
  gboolean outline;
  get_corners(window, &radius, &outline);
  if (radius != corners->radius || outline != corners->outline) {
    corners->radius = radius;
    corners->outline = outline;
    g_autoptr(FlValue) args = corners_value(radius, outline);
    fl_method_channel_invoke_method(corners->channel, "cornersChanged", args,
                                    nullptr, nullptr, nullptr);
  }

  // libhandy masks the corners until Flutter has drawn the current ones.
  if (corners->drawn_radius != radius) {
    return FALSE;
  }
  corners->window_draw(window, cr);
  return TRUE;
}

static void corners_method_cb(FlMethodChannel*, FlMethodCall* call,
                              gpointer user_data) {
  HandyCorners* corners = static_cast<HandyCorners*>(user_data);
  const gchar* method = fl_method_call_get_name(call);
  g_autoptr(GError) error = nullptr;
  gboolean responded;
  if (g_strcmp0(method, "getCorners") == 0) {
    // Clipped corners must show through, not show the view's background,
    // which is opaque black by default. It stays transparent afterwards.
    const GdkRGBA transparent = {};
    fl_view_set_background_color(corners->view, &transparent);
    get_corners(corners->window, &corners->radius, &corners->outline);
    corners->drawn_radius = -1;
    g_autoptr(FlValue) value = corners_value(corners->radius, corners->outline);
    responded = fl_method_call_respond_success(call, value, &error);
  } else if (g_strcmp0(method, "cornersDrawn") == 0) {
    FlValue* args = fl_method_call_get_args(call);
    if (fl_value_get_type(args) == FL_VALUE_TYPE_INT) {
      corners->drawn_radius = fl_value_get_int(args);
    } else {  // null: Flutter hands the corners back to libhandy
      corners->radius = -1;
      corners->drawn_radius = -1;
    }
    // Redraw in the new mode now: until then the outline is doubled or gone.
    gtk_widget_queue_draw(corners->window);
    responded = fl_method_call_respond_success(call, nullptr, &error);
  } else {
    responded = fl_method_call_respond_not_implemented(call, &error);
  }
  if (!responded) {
    g_warning("Failed to respond to %s: %s", method, error->message);
  }
}

// Keeps a method call that races with the window closing from reaching the
// freed HandyCorners.
static void corners_gone_cb(FlMethodChannel*, FlMethodCall* call, gpointer) {
  fl_method_call_respond_not_implemented(call, nullptr);
}

static void free_corners(gpointer data) {
  HandyCorners* corners = static_cast<HandyCorners*>(data);
  // The window outlives its data when setup_corners replaces it.
  g_signal_handlers_disconnect_by_func(
      corners->window, (gpointer) G_CALLBACK(corners_draw_cb), corners);
  fl_method_channel_set_method_call_handler(corners->channel, corners_gone_cb,
                                            nullptr, nullptr);
  g_object_unref(corners->channel);
  g_free(corners);
}

static void setup_corners(FlView* view, FlBinaryMessenger* messenger,
                          gboolean (*window_draw)(GtkWidget*, cairo_t*)) {
  GtkWidget* window = gtk_widget_get_toplevel(GTK_WIDGET(view));
  HandyCorners* corners = g_new0(HandyCorners, 1);
  corners->view = view;
  corners->window = window;
  corners->window_draw = window_draw;
  corners->radius = -1;
  corners->drawn_radius = -1;
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  corners->channel = fl_method_channel_new(messenger, "handy_window",
                                           FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(corners->channel, corners_method_cb,
                                            corners, nullptr);
  g_signal_connect(window, "draw", G_CALLBACK(corners_draw_cb), corners);
  g_object_set_data_full(G_OBJECT(window), "_handy_window_corners_", corners,
                         free_corners);
}

static void setup_handy_window(FlPluginRegistrar* registrar) {
  FlView* view = fl_plugin_registrar_get_view(registrar);
  FlBinaryMessenger* messenger = fl_plugin_registrar_get_messenger(registrar);
  GtkWidget* window = gtk_widget_get_toplevel(GTK_WIDGET(view));

  if (!use_csd(window)) {
    return;
  }

#ifndef USE_LIBHANDY
  if (!sanity_check_window(window) || !sanity_check_view(view)) {
    g_warning(
        "Setting up a Handy window failed. A normal GTK window will be used "
        "instead. See README.md for more detailed instructions.");
    return;
  }

  // titlebar
  GtkWidget* header_bar = nullptr;
  GtkWidget* titlebar = gtk_window_get_titlebar(GTK_WINDOW(window));
  const gchar* title = gtk_window_get_title(GTK_WINDOW(window));
  if (GTK_IS_HEADER_BAR(titlebar)) {
    header_bar = hdy_header_bar_new();
    copy_header_bar(HDY_HEADER_BAR(header_bar), GTK_HEADER_BAR(titlebar));
    gtk_window_set_titlebar(GTK_WINDOW(window), nullptr);
  } else if (title != nullptr) {
    header_bar = hdy_header_bar_new();
    hdy_header_bar_set_title(HDY_HEADER_BAR(header_bar), title);
    hdy_header_bar_set_show_close_button(HDY_HEADER_BAR(header_bar), TRUE);
    gtk_window_set_title(GTK_WINDOW(window), nullptr);
  }
  if (header_bar != nullptr) {
    g_signal_connect(G_OBJECT(window), "notify::title",
                     G_CALLBACK(update_header_bar_title), header_bar);
    g_signal_connect(G_OBJECT(window), "notify::deletable",
                     G_CALLBACK(update_header_bar_buttons), header_bar);
  }

  // view
  gtk_widget_hide(GTK_WIDGET(view));
  g_object_ref(G_OBJECT(view));
  gtk_container_remove(GTK_CONTAINER(window), GTK_WIDGET(view));

  // mixin
  hdy_init();
  HdyWindowMixin* mixin =
      hdy_window_mixin_new(GTK_WINDOW(window), GTK_WINDOW_GET_CLASS(window));
  set_window_mixin(window, mixin);

  // member functions
  GObjectClass* object_class = G_OBJECT_GET_CLASS(window);
  GtkWidgetClass* widget_class = GTK_WIDGET_GET_CLASS(window);
  GtkContainerClass* container_class = GTK_CONTAINER_GET_CLASS(window);
  // original
  gtk_window_finalize = object_class->finalize;
  gtk_window_draw = widget_class->draw;
  gtk_window_destroy = widget_class->destroy;
  gtk_window_add = container_class->add;
  gtk_window_remove = container_class->remove;
  gtk_window_forall = container_class->forall;
  // override
  object_class->finalize = hdy_window_finalize;
  widget_class->draw = hdy_window_draw;
  widget_class->destroy = hdy_window_destroy;
  container_class->add = hdy_window_add;
  container_class->remove = hdy_window_remove;
  container_class->forall = hdy_window_forall;

  // layout
  GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
  gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(box));
  if (header_bar != nullptr) {
    gtk_box_pack_start(GTK_BOX(box), GTK_WIDGET(header_bar), false, true, 0);
  }
  gtk_box_pack_end(GTK_BOX(box), GTK_WIDGET(view), true, true, 0);

  gtk_widget_show(window);
  gtk_widget_show(box);
  if (header_bar != nullptr) {
    gtk_widget_show(header_bar);
  }
  gtk_widget_show(GTK_WIDGET(view));

  // Flutter can draw the corners when its view fills the window.
  if (header_bar == nullptr) {
    setup_corners(view, messenger, gtk_window_draw);
  }
#else
  // Flutter can draw the corners when its view is the Handy window's child.
  // libhandy chains up to GtkWindow's draw when it does not mask them.
  if ((HDY_IS_APPLICATION_WINDOW(window) || HDY_IS_WINDOW(window)) &&
      gtk_widget_get_parent(gtk_widget_get_parent(GTK_WIDGET(view))) ==
          window) {
    setup_corners(view, messenger,
                  GTK_WIDGET_CLASS(g_type_class_peek(GTK_TYPE_WINDOW))->draw);
  }
#endif  // USE_LIBHANDY

  // css
  HandySettings* settings = handy_settings_new();
  load_css(settings);
  g_signal_connect_object(settings, "changed", G_CALLBACK(load_css), settings,
                          G_CONNECT_SWAPPED);
}

void handy_window_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
  setup_handy_window(registrar);
}
#else
void handy_window_plugin_register_with_registrar(FlPluginRegistrar*) {}
#endif
