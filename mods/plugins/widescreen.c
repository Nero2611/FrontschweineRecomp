/*
 * Game-owned widescreen mod for Frontschweine (SLES-02767).
 *
 * The framework keeps the display aspect mod-owned on PSX; this trusted
 * plugin selects it for the manifest in
 * mods/preloaded/packages/frontschweine.presentation.widescreen.
 */
#include "mod_plugins.h"

#include <string.h>

#define PACKAGE_ID "frontschweine.presentation.widescreen"
#define FEATURE_ID "widescreen"

static void widescreen_activate(void) {
    char aspect[16] = "16:9";
    (void)psx_mod_option_value(PACKAGE_ID, FEATURE_ID, "aspect", aspect, sizeof aspect);
    if (strcmp(aspect, "21:9") == 0)
        (void)psx_mod_set_fixed_display_aspect(21u, 9u);
    else
        (void)psx_mod_set_fixed_display_aspect(16u, 9u);
}

PSX_MOD_CONSTRUCTOR(frontschweine_register_widescreen_plugin) {
    (void)psx_mod_register_activation_plugin("frontschweine.widescreen",
                                             widescreen_activate);
}
