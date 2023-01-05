// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handy_settings_portal.h"

#include <gio/gio.h>
#include <glib.h>

static constexpr char kPortalName[] = "org.freedesktop.portal.Desktop";
static constexpr char kPortalPath[] = "/org/freedesktop/portal/desktop";
static constexpr char kPortalSettings[] = "org.freedesktop.portal.Settings";

struct HandySetting {
  const gchar* ns;
  const gchar* key;
  const GVariantType* type;
};

static constexpr char kXdgAppearance[] = "org.freedesktop.appearance";
static const HandySetting kColorScheme = {
    kXdgAppearance,
    "color-scheme",
    G_VARIANT_TYPE_UINT32,
};

static constexpr char kGnomeDesktopInterface[] = "org.gnome.desktop.interface";
static const HandySetting kGtkTheme = {
    kGnomeDesktopInterface,
    "gtk-theme",
    G_VARIANT_TYPE_STRING,
};

static const HandySetting kAllSettings[] = {kColorScheme, kGtkTheme};

static constexpr char kGtkThemeDarkSuffix[] = "-dark";

typedef enum { kDefault, kPreferDark, kPreferLight } ColorScheme;

struct _HandySettingsPortal {
  GObject parent_instance;

  GDBusProxy* dbus_proxy;
  GVariantDict* values;
};

static void handy_settings_portal_iface_init(HandySettingsInterface* iface);

G_DEFINE_TYPE_WITH_CODE(HandySettingsPortal, handy_settings_portal,
                        G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(handy_settings_get_type(),
                                              handy_settings_portal_iface_init))

static gchar* format_key(const HandySetting* setting) {
  return g_strconcat(setting->ns, "::", setting->key, nullptr);
}

static gboolean get_value(HandySettingsPortal* portal,
                          const HandySetting* setting, GVariant** value) {
  g_autofree const gchar* key = format_key(setting);
  *value = g_variant_dict_lookup_value(portal->values, key, setting->type);
  return *value != nullptr;
}

static void set_value(HandySettingsPortal* portal, const HandySetting* setting,
                      GVariant* value) {
  g_autofree const gchar* key = format_key(setting);

  // ignore redundant changes from multiple XDG desktop portal backends
  g_autoptr(GVariant) old_value =
      g_variant_dict_lookup_value(portal->values, key, nullptr);
  if (old_value != nullptr && value != nullptr &&
      g_variant_equal(old_value, value)) {
    return;
  }

  g_variant_dict_insert_value(portal->values, key, value);
  handy_settings_emit_changed(HANDY_SETTINGS(portal));
}

// Based on
// https://gitlab.gnome.org/GNOME/Initiatives/-/wikis/Dark-Style-Preference#other
static gboolean settings_portal_read(GDBusProxy* proxy, const gchar* ns,
                                     const gchar* key, GVariant** out) {
  g_autoptr(GError) error = nullptr;
  g_autoptr(GVariant) value =
      g_dbus_proxy_call_sync(proxy, "Read", g_variant_new("(ss)", ns, key),
                             G_DBUS_CALL_FLAGS_NONE, G_MAXINT, nullptr, &error);

  if (error) {
    if (error->domain == G_DBUS_ERROR &&
        error->code == G_DBUS_ERROR_SERVICE_UNKNOWN) {
      g_debug("XDG desktop portal unavailable: %s", error->message);
      return false;
    }

    if (error->domain == G_DBUS_ERROR &&
        error->code == G_DBUS_ERROR_UNKNOWN_METHOD) {
      g_debug("XDG desktop portal settings unavailable: %s", error->message);
      return false;
    }

    g_critical("Failed to read XDG desktop portal settings: %s",
               error->message);
    return false;
  }

  g_autoptr(GVariant) child = nullptr;
  g_variant_get(value, "(v)", &child);
  g_variant_get(child, "v", out);

  return true;
}

static void settings_portal_changed_cb(GDBusProxy* /*proxy*/,
                                       const char* /*sender_name*/,
                                       const char* signal_name,
                                       GVariant* parameters,
                                       gpointer user_data) {
  HandySettingsPortal* portal = HANDY_SETTINGS_PORTAL(user_data);
  if (g_strcmp0(signal_name, "SettingChanged")) {
    return;
  }

  HandySetting setting;
  g_autoptr(GVariant) value = nullptr;
  g_variant_get(parameters, "(&s&sv)", &setting.ns, &setting.key, &value);
  set_value(portal, &setting, value);
}

static HandyColorScheme handy_settings_portal_get_color_scheme(
    HandySettings* settings) {
  HandySettingsPortal* self = HANDY_SETTINGS_PORTAL(settings);

  HandyColorScheme color_scheme = HANDY_COLOR_SCHEME_LIGHT;

  g_autoptr(GVariant) value = nullptr;
  if (get_value(self, &kColorScheme, &value)) {
    if (g_variant_get_uint32(value) == kPreferDark) {
      color_scheme = HANDY_COLOR_SCHEME_DARK;
    }
  } else if (get_value(self, &kGtkTheme, &value)) {
    const gchar* gtk_theme_str = g_variant_get_string(value, nullptr);
    if (g_str_has_suffix(gtk_theme_str, kGtkThemeDarkSuffix)) {
      color_scheme = HANDY_COLOR_SCHEME_DARK;
    }
  }

  return color_scheme;
}

static void handy_settings_portal_dispose(GObject* object) {
  HandySettingsPortal* self = HANDY_SETTINGS_PORTAL(object);

  g_clear_object(&self->dbus_proxy);
  g_clear_pointer(&self->values, g_variant_dict_unref);

  G_OBJECT_CLASS(handy_settings_portal_parent_class)->dispose(object);
}

static void handy_settings_portal_class_init(HandySettingsPortalClass* klass) {
  GObjectClass* object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = handy_settings_portal_dispose;
}

static void handy_settings_portal_iface_init(HandySettingsInterface* iface) {
  iface->get_color_scheme = handy_settings_portal_get_color_scheme;
}

static void handy_settings_portal_init(HandySettingsPortal*) {}

HandySettingsPortal* handy_settings_portal_new() {
  g_autoptr(GVariantDict) values = g_variant_dict_new(nullptr);
  return handy_settings_portal_new_with_values(values);
}

HandySettingsPortal* handy_settings_portal_new_with_values(
    GVariantDict* values) {
  g_return_val_if_fail(values != nullptr, nullptr);
  HandySettingsPortal* portal = HANDY_SETTINGS_PORTAL(
      g_object_new(handy_settings_portal_get_type(), nullptr));
  portal->values = g_variant_dict_ref(values);
  return portal;
}

gboolean handy_settings_portal_start(HandySettingsPortal* self,
                                     GError** error) {
  g_return_val_if_fail(HANDY_IS_SETTINGS_PORTAL(self), false);
  g_return_val_if_fail(self->dbus_proxy == nullptr, false);

  self->dbus_proxy = g_dbus_proxy_new_for_bus_sync(
      G_BUS_TYPE_SESSION, G_DBUS_PROXY_FLAGS_NONE, nullptr, kPortalName,
      kPortalPath, kPortalSettings, nullptr, error);

  if (self->dbus_proxy == nullptr) {
    return false;
  }

  for (const HandySetting setting : kAllSettings) {
    g_autoptr(GVariant) value = nullptr;
    if (settings_portal_read(self->dbus_proxy, setting.ns, setting.key,
                             &value)) {
      set_value(self, &setting, value);
    }
  }

  g_signal_connect_object(self->dbus_proxy, "g-signal",
                          G_CALLBACK(settings_portal_changed_cb), self,
                          static_cast<GConnectFlags>(0));

  return true;
}
