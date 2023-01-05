// Copyright 2013 The Flutter Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef HANDY_SETTINGS_PORTAL_H_
#define HANDY_SETTINGS_PORTAL_H_

#include "handy_settings.h"

G_BEGIN_DECLS

G_DECLARE_FINAL_TYPE(HandySettingsPortal, handy_settings_portal, HANDY,
                     SETTINGS_PORTAL, GObject);

/**
 * HandySettingsPortal:
 * #HandySettingsPortal reads settings from the XDG desktop portal.
 */

/**
 * handy_settings_portal_new:
 *
 * Creates a new settings portal instance.
 *
 * Returns: a new #HandySettingsPortal.
 */
HandySettingsPortal* handy_settings_portal_new();

/**
 * handy_settings_portal_new_with_values:
 * @values: (nullable): a #GVariantDict.
 *
 * Creates a new settings portal instance with initial values for testing.
 *
 * Returns: a new #HandySettingsPortal.
 */
HandySettingsPortal* handy_settings_portal_new_with_values(
    GVariantDict* values);

/**
 * handy_settings_portal_start:
 * @portal: an #HandySettingsPortal.
 * @error: (allow-none): #GError location to store the error occurring, or %NULL
 *
 * Reads the current settings and starts monitoring for changes in the desktop
 * portal settings.
 *
 * Returns: %TRUE on success, or %FALSE if the portal is not available.
 */
gboolean handy_settings_portal_start(HandySettingsPortal* portal,
                                     GError** error);

G_END_DECLS

#endif  // HANDY_SETTINGS_PORTAL_H_
