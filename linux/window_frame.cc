#include "window_frame.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

// libhandy's corner masking copies frames off the GPU (ubuntu/app-center#2080).

static constexpr char kChannelName[] = "handy_window/window_frame";
static constexpr char kDataKey[] = "handy-window-frame";

// Corners may reach this far past the radius; libhandy's rim goes 1px past.
static constexpr int kMaxCornerOverhang = 4;

// Extremes and mid grey, plus Yaru's orange and dark greys.
static constexpr uint32_t kRimTestBackdrops[] = {
    0xff000000, 0xffffffff, 0xff808080, 0xffe95420, 0xff252525, 0xff3d3d3d};

// A square image in cairo's ARGB32: native-endian, premultiplied.
struct Pixels {
  int size = 0;
  std::vector<uint32_t> data;

  uint32_t at(int x, int y) const { return data[y * size + x]; }
};

struct WindowFrame {
  GtkWindow* window;
  FlView* view;
  FlMethodChannel* channel;
  // Stops libhandy's masking; its decoration nodes keep the theme's radius.
  GtkCssProvider* unrounded;
  gulong first_frame_handler = 0;
  gulong block_draw_handler = 0;
  bool enabled = false;
  bool taken_over = false;
  bool had_first_frame = false;
  FlValue* style = nullptr;
};

// pixman's x * y / 255, rounded; the Dart side checks the GPU rounds alike.
static uint32_t mul(uint32_t x, uint32_t y) {
  uint32_t t = x * y + 0x80;
  return ((t >> 8) + t) >> 8;
}

static uint32_t over(uint32_t source, uint32_t destination) {
  uint32_t inverse_alpha = 255 - (source >> 24);
  uint32_t result = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    uint32_t channel = ((source >> shift) & 0xff) +
                       mul((destination >> shift) & 0xff, inverse_alpha);
    result |= channel << shift;
  }
  return result;
}

// Draws `context` over `backdrop` the way libhandy draws its nodes.
static bool render(GtkStyleContext* context,
                   int size,
                   int scale,
                   uint32_t backdrop,
                   Pixels* pixels) {
  int device_size = size * scale;
  cairo_surface_t* surface = cairo_image_surface_create(
      CAIRO_FORMAT_ARGB32, device_size, device_size);
  bool rendered = cairo_surface_status(surface) == CAIRO_STATUS_SUCCESS;
  if (rendered) {
    unsigned char* data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < device_size; y++) {
      uint32_t* row = reinterpret_cast<uint32_t*>(data + y * stride);
      std::fill(row, row + device_size, backdrop);
    }
    cairo_surface_mark_dirty(surface);
    cairo_surface_set_device_scale(surface, scale, scale);
    cairo_t* cr = cairo_create(surface);
    gtk_render_background(context, cr, 0, 0, size, size);
    gtk_render_frame(context, cr, 0, 0, size, size);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    pixels->size = device_size;
    pixels->data.resize(device_size * device_size);
    for (int y = 0; y < device_size; y++) {
      memcpy(&pixels->data[y * device_size], data + y * stride,
             device_size * sizeof(uint32_t));
    }
  }
  cairo_surface_destroy(surface);
  return rendered;
}

// Styled like the window, minus this file's radius override.
static GtkStyleContext* new_window_probe(GtkWidget* window, int scale) {
  GtkStyleContext* probe = gtk_style_context_new();
  gtk_style_context_set_screen(probe, gtk_widget_get_screen(window));
  GtkWidgetPath* path = gtk_widget_path_new();
  gtk_widget_path_append_for_widget(path, window);
  gtk_style_context_set_path(probe, path);
  gtk_widget_path_unref(path);
  gtk_style_context_set_state(
      probe, gtk_style_context_get_state(gtk_widget_get_style_context(window)));
  gtk_style_context_set_scale(probe, scale);
  return probe;
}

// Set up like libhandy's create_child_context() and update_child_context().
static GtkStyleContext* new_child_probe(GtkWidget* window, const char* name) {
  GtkStyleContext* parent = gtk_widget_get_style_context(window);
  GtkStyleContext* probe = gtk_style_context_new();
  gtk_style_context_set_parent(probe, parent);
  gtk_style_context_set_screen(probe, gtk_style_context_get_screen(parent));
  gtk_style_context_set_frame_clock(probe,
                                    gtk_style_context_get_frame_clock(parent));
  GtkWidgetPath* path = gtk_widget_path_new();
  gtk_widget_path_append_for_widget(path, window);
  gint position = gtk_widget_path_append_type(path, GTK_TYPE_WIDGET);
  gtk_widget_path_iter_set_object_name(path, position, name);
  gtk_style_context_set_path(probe, path);
  gtk_widget_path_unref(path);
  gtk_style_context_set_state(probe, gtk_style_context_get_state(parent));
  return probe;
}

// The top-left radius, which is the one libhandy reads.
static int border_radius(GtkStyleContext* context) {
  int radius = 0;
  gtk_style_context_get(context, gtk_style_context_get_state(context),
                        GTK_STYLE_PROPERTY_BORDER_RADIUS, &radius, nullptr);
  return radius;
}

static Pixels nine_slice(const Pixels& pixels, int inset) {
  Pixels atlas;
  atlas.size = 2 * inset + 1;
  atlas.data.resize(atlas.size * atlas.size);
  auto source = [&](int i) {
    return i <= inset ? i : i - atlas.size + pixels.size;
  };
  for (int y = 0; y < atlas.size; y++) {
    for (int x = 0; x < atlas.size; x++) {
      atlas.data[y * atlas.size + x] = pixels.at(source(x), source(y));
    }
  }
  return atlas;
}

// Whether Canvas.drawImageNine would stretch `atlas` back into `pixels`.
static bool rebuilds(const Pixels& atlas, int inset, const Pixels& pixels) {
  auto source = [&](int i) {
    if (i < inset) {
      return i;
    }
    if (i >= pixels.size - inset) {
      return i - pixels.size + atlas.size;
    }
    return inset;
  };
  for (int y = 0; y < pixels.size; y++) {
    for (int x = 0; x < pixels.size; x++) {
      if (pixels.at(x, y) != atlas.at(source(x), source(y))) {
        return false;
      }
    }
  }
  return true;
}

static FlValue* premultiplied_rgba(const Pixels& pixels) {
  std::vector<uint8_t> bytes;
  bytes.reserve(pixels.data.size() * 4);
  for (uint32_t pixel : pixels.data) {
    bytes.push_back((pixel >> 16) & 0xff);
    bytes.push_back((pixel >> 8) & 0xff);
    bytes.push_back(pixel & 0xff);
    bytes.push_back(pixel >> 24);
  }
  return fl_value_new_uint8_list(bytes.data(), bytes.size());
}

// Drawn as libhandy's create_masks() draws them, in reading order.
static FlValue* corner_masks(int radius, int scale) {
  int size = radius * scale;
  double correction = 0.5 / scale;
  double r = radius - correction;
  std::vector<uint8_t> bytes;
  bytes.reserve(4 * size * size);
  for (int i = 0; i < 4; i++) {
    cairo_surface_t* surface =
        cairo_image_surface_create(CAIRO_FORMAT_A8, size, size);
    cairo_t* cr = cairo_create(surface);
    cairo_scale(cr, scale, scale);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_arc(cr, i % 2 == 0 ? r : correction, i / 2 == 0 ? r : correction, r,
              0, G_PI * 2);
    cairo_fill(cr);
    cairo_destroy(cr);
    cairo_surface_flush(surface);
    unsigned char* data = cairo_image_surface_get_data(surface);
    int stride = cairo_image_surface_get_stride(surface);
    for (int y = 0; y < size; y++) {
      bytes.insert(bytes.end(), data + y * stride, data + y * stride + size);
    }
    cairo_surface_destroy(surface);
  }
  return fl_value_new_uint8_list(bytes.data(), bytes.size());
}

// Dart blends the rim as one image; GTK's layers must blend the same way.
static bool rim_composites_exactly(GtkStyleContext* rim_context,
                                   int size,
                                   int scale,
                                   const Pixels& rim) {
  for (uint32_t backdrop : kRimTestBackdrops) {
    Pixels composited;
    if (!render(rim_context, size, scale, backdrop, &composited)) {
      return false;
    }
    for (size_t i = 0; i < composited.data.size(); i++) {
      if (composited.data[i] != over(rim.data[i], backdrop)) {
        return false;
      }
    }
  }
  return true;
}

// Dart paints only inside the view, so it must cover all that libhandy masks.
static bool view_fills_window(WindowFrame* self) {
  GtkWidget* window = GTK_WIDGET(self->window);
  GtkWidget* content = gtk_bin_get_child(GTK_BIN(window));
  GtkWidget* titlebar = gtk_window_get_titlebar(self->window);
  GtkWidget* view = GTK_WIDGET(self->view);
  int content_x, content_y, view_x, view_y;
  return content != nullptr &&
         (titlebar == nullptr || !gtk_widget_get_visible(titlebar)) &&
         gtk_widget_translate_coordinates(content, window, 0, 0, &content_x,
                                          &content_y) &&
         gtk_widget_translate_coordinates(view, window, 0, 0, &view_x,
                                          &view_y) &&
         view_x == content_x && view_y == content_y &&
         gtk_widget_get_allocated_width(view) ==
             gtk_widget_get_allocated_width(content) &&
         gtk_widget_get_allocated_height(view) ==
             gtk_widget_get_allocated_height(content);
}

// Dart matches pixman, which composites image surfaces; X11 uses XRender.
static bool window_surface_is_image(GdkWindow* gdk_window) {
  cairo_surface_t* surface = gdk_window_create_similar_surface(
      gdk_window, CAIRO_CONTENT_COLOR_ALPHA, 1, 1);
  bool is_image = cairo_surface_get_type(surface) == CAIRO_SURFACE_TYPE_IMAGE;
  cairo_surface_destroy(surface);
  return is_image;
}

// 3.24.39-41 skip GL blending, which wipes the corner shadow: GNOME/gtk#6359.
static bool gtk_blends_gl_textures() {
  guint micro = gtk_get_micro_version();
  return gtk_get_minor_version() != 24 || micro < 39 || micro > 41;
}

// nullptr if Dart can't paint the current style exactly.
static FlValue* compute_style(WindowFrame* self) {
  GtkWidget* window = GTK_WIDGET(self->window);
  GdkWindow* gdk_window = gtk_widget_get_window(window);
  if (gdk_window == nullptr || !window_surface_is_image(gdk_window) ||
      !gtk_blends_gl_textures()) {
    return nullptr;
  }
  GtkStyleContext* context = gtk_widget_get_style_context(window);
  // solid-csd borders come from GtkWindow, which skips them if app-paintable.
  if (gtk_style_context_has_class(context, "solid-csd") ||
      !view_fills_window(self)) {
    return nullptr;
  }

  int scale = gdk_window_get_scale_factor(gdk_window);
  g_autoptr(GtkStyleContext) window_probe = new_window_probe(window, scale);
  int radius = border_radius(window_probe);
  bool fullscreen =
      gdk_window_get_state(gdk_window) & GDK_WINDOW_STATE_FULLSCREEN;
  // libhandy's conditions for masking, from hdy_window_mixin_draw().
  bool masked = gtk_window_get_decorated(self->window) && !fullscreen &&
                !gtk_window_is_maximized(self->window) && radius > 0;

  int max_inset = (radius + kMaxCornerOverhang) * scale;
  // Rendered at two sizes, so that the slicing can't depend on the size.
  int small_size = 2 * (radius + kMaxCornerOverhang) + 1;
  int large_size = 2 * small_size + 3;
  Pixels background, large_background;
  if (!render(window_probe, small_size, scale, 0, &background) ||
      !render(window_probe, large_size, scale, 0, &large_background)) {
    return nullptr;
  }
  if (!self->taken_over) {
    // Once taken over, the real context has the override the probe lacks.
    Pixels real_background;
    if (!render(context, small_size, scale, 0, &real_background) ||
        real_background.data != background.data) {
      return nullptr;
    }
  }

  Pixels rim, large_rim;
  if (masked) {
    g_autoptr(GtkStyleContext) rim_probe =
        new_child_probe(window, "decoration-overlay");
    if (!render(rim_probe, small_size, scale, 0, &rim) ||
        !render(rim_probe, large_size, scale, 0, &large_rim) ||
        !rim_composites_exactly(rim_probe, small_size, scale, rim)) {
      return nullptr;
    }
  }

  for (int inset = 0; inset <= max_inset; inset++) {
    Pixels background_atlas = nine_slice(background, inset);
    if (!rebuilds(background_atlas, inset, background) ||
        !rebuilds(background_atlas, inset, large_background)) {
      continue;
    }
    Pixels rim_atlas;
    if (masked) {
      rim_atlas = nine_slice(rim, inset);
      if (!rebuilds(rim_atlas, inset, rim) ||
          !rebuilds(rim_atlas, inset, large_rim)) {
        continue;
      }
    }
    FlValue* style = fl_value_new_map();
    fl_value_set_string_take(style, "scale", fl_value_new_int(scale));
    fl_value_set_string_take(style, "inset", fl_value_new_int(inset));
    fl_value_set_string_take(style, "background",
                             premultiplied_rgba(background_atlas));
    if (masked) {
      fl_value_set_string_take(style, "rim", premultiplied_rgba(rim_atlas));
      fl_value_set_string_take(style, "radius",
                               fl_value_new_int(radius * scale));
      fl_value_set_string_take(style, "masks", corner_masks(radius, scale));
    }
    return style;
  }
  return nullptr;
}

// Mirrors gtk_window_update_opaque_region(), which skips app-paintable windows.
static void update_opaque_region(WindowFrame* self) {
  if (!self->taken_over) {
    return;
  }
  GtkWidget* window = GTK_WIDGET(self->window);
  GdkWindow* gdk_window = gtk_widget_get_window(window);
  if (gdk_window == nullptr) {
    return;
  }
  GtkStyleContext* context = gtk_widget_get_style_context(window);
  GdkRGBA* background = nullptr;
  gtk_style_context_get(context, gtk_style_context_get_state(context),
                        GTK_STYLE_PROPERTY_BACKGROUND_COLOR, &background,
                        nullptr);
  bool opaque =
      background->alpha >= 1.0 && gtk_widget_get_opacity(window) >= 1.0;
  gdk_rgba_free(background);
  if (!opaque) {
    gdk_window_set_opaque_region(gdk_window, nullptr);
    return;
  }

  GtkWidget* content = GTK_WIDGET(self->view);
  cairo_rectangle_int_t bounds;
  if (!gtk_widget_translate_coordinates(content, window, 0, 0, &bounds.x,
                                        &bounds.y)) {
    return;
  }
  bounds.width = gtk_widget_get_allocated_width(content);
  bounds.height = gtk_widget_get_allocated_height(content);
  cairo_region_t* region = cairo_region_create_rectangle(&bounds);
  GdkWindowState state = gdk_window_get_state(gdk_window);
  if (gtk_style_context_has_class(context, "csd") &&
      gtk_window_get_decorated(self->window) &&
      !(state & GDK_WINDOW_STATE_FULLSCREEN) &&
      !gtk_window_is_maximized(self->window)) {
    // Public API only gives the top-left radius; Yaru rounds all four alike.
    g_autoptr(GtkStyleContext) window_probe =
        new_window_probe(window, gdk_window_get_scale_factor(gdk_window));
    g_autoptr(GtkStyleContext) decoration_probe =
        new_child_probe(window, "decoration");
    int corner =
        std::max(border_radius(window_probe), border_radius(decoration_probe));
    int right = bounds.x + bounds.width - corner;
    int bottom = bounds.y + bounds.height - corner;
    cairo_rectangle_int_t corners[] = {{bounds.x, bounds.y, corner, corner},
                                       {right, bounds.y, corner, corner},
                                       {bounds.x, bottom, corner, corner},
                                       {right, bottom, corner, corner}};
    for (const cairo_rectangle_int_t& square : corners) {
      cairo_region_subtract_rectangle(region, &square);
    }
  }
  gdk_window_set_opaque_region(gdk_window, region);
  cairo_region_destroy(region);
}

static void take_over(WindowFrame* self) {
  self->taken_over = true;
  GtkWidget* window = GTK_WIDGET(self->window);
  gtk_widget_set_app_paintable(window, TRUE);
  gtk_style_context_add_provider(gtk_widget_get_style_context(window),
                                 GTK_STYLE_PROVIDER(self->unrounded),
                                 GTK_STYLE_PROVIDER_PRIORITY_USER + 1);
  // An opaque view background would fill the corners Dart cuts away.
  const GdkRGBA transparent{};
  fl_view_set_background_color(self->view, &transparent);
  update_opaque_region(self);
}

// Returns the corners to libhandy for the rest of the session.
static void give_back(WindowFrame* self) {
  self->enabled = false;
  if (self->taken_over) {
    self->taken_over = false;
    GtkWidget* window = GTK_WIDGET(self->window);
    gtk_widget_set_app_paintable(window, FALSE);
    gtk_style_context_remove_provider(gtk_widget_get_style_context(window),
                                      GTK_STYLE_PROVIDER(self->unrounded));
    // FlView can't report its old background, so the view stays transparent.
  }
  g_clear_pointer(&self->style, fl_value_unref);
  fl_method_channel_invoke_method(self->channel, "styleChanged", nullptr,
                                  nullptr, nullptr, nullptr);
}

static void style_changed(WindowFrame* self) {
  if (!self->enabled) {
    return;
  }
  FlValue* style = compute_style(self);
  if (style == nullptr) {
    give_back(self);
    return;
  }
  if (fl_value_equal(style, self->style)) {
    fl_value_unref(style);
    return;
  }
  fl_value_unref(self->style);
  self->style = style;
  fl_method_channel_invoke_method(self->channel, "styleChanged", style,
                                  nullptr, nullptr, nullptr);
}

// Until first-frame, libhandy could cut Dart's corners twice; no frame to lose.
static gboolean block_draw_cb(GtkWidget*, cairo_t*, gpointer) {
  return TRUE;
}

static void method_call_cb(FlMethodChannel*,
                           FlMethodCall* method_call,
                           gpointer user_data) {
  auto* self = static_cast<WindowFrame*>(user_data);
  const gchar* method = fl_method_call_get_name(method_call);
  if (strcmp(method, "enable") == 0) {
    // Too late after the first frame: it was painted for libhandy's path.
    if (!self->enabled && !self->had_first_frame) {
      // Dart can ask before GTK's first layout, when the view has no size yet.
      gtk_container_check_resize(GTK_CONTAINER(self->window));
      self->style = compute_style(self);
      self->enabled = self->style != nullptr;
      if (self->enabled && self->block_draw_handler == 0) {
        self->block_draw_handler = g_signal_connect(
            self->view, "draw", G_CALLBACK(block_draw_cb), nullptr);
      }
    }
    g_autoptr(FlValue) result =
        self->enabled ? fl_value_ref(self->style) : fl_value_new_null();
    fl_method_call_respond_success(method_call, result, nullptr);
  } else if (strcmp(method, "disable") == 0) {
    give_back(self);
    fl_method_call_respond_success(method_call, nullptr, nullptr);
  } else {
    fl_method_call_respond_not_implemented(method_call, nullptr);
  }
}

static void first_frame_cb(FlView* view, gpointer user_data) {
  auto* self = static_cast<WindowFrame*>(user_data);
  self->had_first_frame = true;
  g_signal_handler_disconnect(view, self->first_frame_handler);
  if (self->block_draw_handler != 0) {
    g_signal_handler_disconnect(view, self->block_draw_handler);
    self->block_draw_handler = 0;
  }
  if (self->enabled) {
    take_over(self);
  }
}

static void style_updated_cb(GtkWidget*, gpointer user_data) {
  auto* self = static_cast<WindowFrame*>(user_data);
  style_changed(self);
  update_opaque_region(self);
}

static gboolean window_state_event_cb(GtkWidget*,
                                      GdkEventWindowState*,
                                      gpointer user_data) {
  style_changed(static_cast<WindowFrame*>(user_data));
  return FALSE;
}

static void scale_factor_cb(GObject*, GParamSpec*, gpointer user_data) {
  style_changed(static_cast<WindowFrame*>(user_data));
}

static void size_allocate_cb(GtkWidget*, GdkRectangle*, gpointer user_data) {
  auto* self = static_cast<WindowFrame*>(user_data);
  if (self->enabled && !view_fills_window(self)) {
    give_back(self);
    return;
  }
  update_opaque_region(self);
}

static void window_frame_free(gpointer data) {
  auto* self = static_cast<WindowFrame*>(data);
  g_object_unref(self->channel);
  g_object_unref(self->unrounded);
  g_clear_pointer(&self->style, fl_value_unref);
  delete self;
}

void window_frame_attach(GtkWindow* window, FlView* view) {
  auto* self = new WindowFrame();
  self->window = window;
  self->view = view;
  // Not a libhandy patch, so linux/libhandy/upgrade.sh keeps pulling upstream.
  self->unrounded = gtk_css_provider_new();
  gtk_css_provider_load_from_data(self->unrounded,
                                  "window { border-radius: 0; }", -1, nullptr);
  g_autoptr(FlStandardMethodCodec) codec = fl_standard_method_codec_new();
  self->channel = fl_method_channel_new(
      fl_engine_get_binary_messenger(fl_view_get_engine(view)), kChannelName,
      FL_METHOD_CODEC(codec));
  fl_method_channel_set_method_call_handler(self->channel, method_call_cb,
                                            self, nullptr);
  self->first_frame_handler =
      g_signal_connect(view, "first-frame", G_CALLBACK(first_frame_cb), self);
  // After GTK's handlers, which restyle the window and reset its opaque region.
  g_signal_connect_after(window, "style-updated", G_CALLBACK(style_updated_cb),
                         self);
  g_signal_connect_after(window, "window-state-event",
                         G_CALLBACK(window_state_event_cb), self);
  g_signal_connect_after(window, "size-allocate", G_CALLBACK(size_allocate_cb),
                         self);
  g_signal_connect(window, "notify::scale-factor", G_CALLBACK(scale_factor_cb),
                   self);
  g_object_set_data_full(G_OBJECT(window), kDataKey, self, window_frame_free);
}
