// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HANDY_SETTINGS_H_
#define HANDY_SETTINGS_H_

#include <glib-object.h>

G_BEGIN_DECLS

G_DECLARE_INTERFACE(HandySettings, handy_settings, HANDY, SETTINGS, GObject)

/**
 * HandyColorScheme:
 * @HANDY_COLOR_SCHEME_LIGHT: Prefer light theme.
 * @HANDY_COLOR_SCHEME_DARK: Prefer dark theme.
 *
 * Available color schemes.
 */
typedef enum {
  HANDY_COLOR_SCHEME_LIGHT,
  HANDY_COLOR_SCHEME_DARK,
} HandyColorScheme;

/**
 * HandySettings:
 * #HandySettings is and object that provides desktop settings.
 */
struct _HandySettingsInterface {
  GTypeInterface parent;
  HandyColorScheme (*get_color_scheme)(HandySettings* settings);
};

/**
 * handy_settings_new:
 *
 * Creates a new settings instance.
 *
 * Returns: a new #HandySettings.
 */
HandySettings* handy_settings_new();

/**
 * handy_settings_get_color_scheme:
 * @settings: an #HandySettings.
 *
 * The preferred color scheme for the user interface.
 *
 * This corresponds to `org.gnome.desktop.interface.color-scheme` in GNOME.
 *
 * Returns: an #HandyColorScheme.
 */
HandyColorScheme handy_settings_get_color_scheme(HandySettings* settings);

/**
 * handy_settings_emit_changed:
 * @settings: an #HandySettings.
 *
 * Emits the "changed" signal. Used by HandySettings implementations to notify
 * when the desktop settings have changed.
 */
void handy_settings_emit_changed(HandySettings* settings);

G_END_DECLS

#endif  // HANDY_SETTINGS_H_
