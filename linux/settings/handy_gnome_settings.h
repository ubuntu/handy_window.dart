// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HANDY_GNOME_SETTINGS_H_
#define HANDY_GNOME_SETTINGS_H_

#include "handy_settings.h"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(HandyGnomeSettings, handy_gnome_settings, HANDY,
                     GNOME_SETTINGS, GObject);

/**
 * handy_gnome_settings_new:
 *
 * Creates a new settings instance for GNOME.
 *
 * Returns: a new #HandySettings.
 */
HandySettings* handy_gnome_settings_new();

G_END_DECLS

#endif  // HANDY_GNOME_SETTINGS_H_
