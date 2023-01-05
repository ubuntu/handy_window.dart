// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handy_gnome_settings.h"

#include <gio/gio.h>
#include <glib.h>

static constexpr char kDesktopInterfaceSchema[] = "org.gnome.desktop.interface";
static constexpr char kDesktopGtkThemeKey[] = "gtk-theme";

static constexpr char kGtkThemeDarkSuffix[] = "-dark";
static constexpr char kInterfaceSettings[] = "interface-settings";

struct _HandyGnomeSettings {
  GObject parent_instance;

  GSettings* interface_settings;
};

enum { kProp0, kPropInterfaceSettings, kPropLast };

static void handy_gnome_settings_iface_init(HandySettingsInterface* iface);

G_DEFINE_TYPE_WITH_CODE(HandyGnomeSettings, handy_gnome_settings, G_TYPE_OBJECT,
                        G_IMPLEMENT_INTERFACE(handy_settings_get_type(),
                                              handy_gnome_settings_iface_init))

static HandyColorScheme handy_gnome_settings_get_color_scheme(
    HandySettings* settings) {
  HandyGnomeSettings* self = HANDY_GNOME_SETTINGS(settings);

  HandyColorScheme color_scheme = HANDY_COLOR_SCHEME_LIGHT;

  if (self->interface_settings != nullptr) {
    // check whether org.gnome.desktop.interface.gtk-theme ends with "-dark"
    g_autofree gchar* value =
        g_settings_get_string(self->interface_settings, kDesktopGtkThemeKey);
    if (g_str_has_suffix(value, kGtkThemeDarkSuffix)) {
      color_scheme = HANDY_COLOR_SCHEME_DARK;
    }
  }
  return color_scheme;
}

static void handy_gnome_settings_set_interface_settings(
    HandyGnomeSettings* self, GSettings* settings) {
  g_return_if_fail(G_IS_SETTINGS(settings));

  g_signal_connect_object(settings, "changed::gtk-theme",
                          G_CALLBACK(handy_settings_emit_changed), self,
                          G_CONNECT_SWAPPED);

  self->interface_settings = G_SETTINGS(g_object_ref(settings));
}

static void handy_gnome_settings_set_property(GObject* object, guint prop_id,
                                              const GValue* value,
                                              GParamSpec* pspec) {
  HandyGnomeSettings* self = HANDY_GNOME_SETTINGS(object);
  switch (prop_id) {
    case kPropInterfaceSettings:
      handy_gnome_settings_set_interface_settings(
          self, G_SETTINGS(g_value_get_object(value)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
      break;
  }
}

static void handy_gnome_settings_dispose(GObject* object) {
  HandyGnomeSettings* self = HANDY_GNOME_SETTINGS(object);

  g_clear_object(&self->interface_settings);

  G_OBJECT_CLASS(handy_gnome_settings_parent_class)->dispose(object);
}

static void handy_gnome_settings_class_init(HandyGnomeSettingsClass* klass) {
  GObjectClass* object_class = G_OBJECT_CLASS(klass);
  object_class->dispose = handy_gnome_settings_dispose;
  object_class->set_property = handy_gnome_settings_set_property;

  g_object_class_install_property(
      object_class, kPropInterfaceSettings,
      g_param_spec_object(
          kInterfaceSettings, kInterfaceSettings, kDesktopInterfaceSchema,
          g_settings_get_type(),
          static_cast<GParamFlags>(G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY |
                                   G_PARAM_STATIC_STRINGS)));
}

static void handy_gnome_settings_iface_init(HandySettingsInterface* iface) {
  iface->get_color_scheme = handy_gnome_settings_get_color_scheme;
}

static void handy_gnome_settings_init(HandyGnomeSettings*) {}

static GSettings* create_settings(const gchar* schema_id) {
  GSettings* settings = nullptr;
  GSettingsSchemaSource* source = g_settings_schema_source_get_default();
  if (source != nullptr) {
    g_autoptr(GSettingsSchema) schema =
        g_settings_schema_source_lookup(source, schema_id, TRUE);
    if (schema != nullptr) {
      settings = g_settings_new_full(schema, nullptr, nullptr);
    }
  }
  return settings;
}

HandySettings* handy_gnome_settings_new() {
  g_autoptr(GSettings) interface_settings =
      create_settings(kDesktopInterfaceSchema);
  return HANDY_SETTINGS(g_object_new(handy_gnome_settings_get_type(),
                                     kInterfaceSettings, interface_settings,
                                     nullptr));
}
