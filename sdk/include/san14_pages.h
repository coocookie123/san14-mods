#ifndef SAN14_MOD_SDK_PAGES_H
#define SAN14_MOD_SDK_PAGES_H

#include "san14_types.h"

/* Optional custom settings entry, one per Mod. Base invokes open on the UI
 * thread through its plugin-fault boundary. Returning SAN14_OK requires at least
 * one plugin panel to have been shown successfully. Base returns to its manager
 * after the last custom panel closes. The plugin owns its layout and behavior. */
typedef San14Result (SAN14_MOD_CALL *San14SettingsPageOpen)(void* plugin_context);

typedef struct San14SettingsPageSpec {
    uint32_t struct_size;
    San14SettingsPageOpen open;
    void* plugin_context;
} San14SettingsPageSpec;

typedef struct San14PagesApi {
    uint32_t struct_size;
    /* Base copies spec. The callback/context stay valid until unregistered.
     * Registration requires ui.panel and an active/loading owner. Duplicate
     * registration returns SAN14_E_BUSY instead of replacing another callback. */
    San14Result (SAN14_MOD_CALL *register_page)(
        void* host_context, const San14SettingsPageSpec* spec);
    /* Release remains available during disable. An absent page returns
     * SAN14_E_NOT_FOUND. Base also removes registrations when cleaning an owner. */
    San14Result (SAN14_MOD_CALL *unregister_page)(void* host_context);
} San14PagesApi;

#endif
