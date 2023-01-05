// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "handy_settings.h"

#include "handy_gnome_settings.h"
#include "handy_settings_portal.h"

G_DEFINE_INTERFACE(HandySettings, handy_settings, G_TYPE_OBJECT)

enum {
  kSignalChanged,
  kSignalLastSignal,
};

static guint signals[kSignalLastSignal];

static void handy_settings_default_init(HandySettingsInterface* iface) {
  /**
   * HandySettings::changed:
   * @settings: an #HandySettings
   *
   * This signal is emitted after the settings have been changed.
   */
  signals[kSignalChanged] =
      g_signal_new("changed", G_TYPE_FROM_INTERFACE(iface), G_SIGNAL_RUN_LAST,
                   0, NULL, NULL, NULL, G_TYPE_NONE, 0);
}

HandyColorScheme handy_settings_get_color_scheme(HandySettings* self) {
  return HANDY_SETTINGS_GET_IFACE(self)->get_color_scheme(self);
}

void handy_settings_emit_changed(HandySettings* self) {
  g_return_if_fail(HANDY_IS_SETTINGS(self));
  g_signal_emit(self, signals[kSignalChanged], 0);
}

HandySettings* handy_settings_new() {
  g_autoptr(HandySettingsPortal) portal = handy_settings_portal_new();

  g_autoptr(GError) error = nullptr;
  if (!handy_settings_portal_start(portal, &error)) {
    g_debug("XDG desktop portal settings unavailable: %s", error->message);
    return handy_gnome_settings_new();
  }

  return HANDY_SETTINGS(g_object_ref(portal));
}
