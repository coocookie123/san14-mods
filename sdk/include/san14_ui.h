#ifndef SAN14_MOD_SDK_UI_H
#define SAN14_MOD_SDK_UI_H

#include "san14_types.h"
#include <stddef.h>

/* Optional label presentation. Coordinates and layout remain plugin-owned. */
#define SAN14_CONTROL_HEADING 1u
#define SAN14_PANEL_CUSTOM_POSITION 1u
/* Modal popup: retain the current owner's visible panel beneath it. Only the
   popup accepts input; Escape/outside click dismiss it. Requires a visible
   panel owned by the same Mod. Older Hosts return UNSUPPORTED. */
#define SAN14_PANEL_POPUP 4u

typedef struct San14PanelSpec {
    uint32_t struct_size;
    San14StringView title;
    int32_t width;
    int32_t height;
    uint32_t flags;
    /* Preserve the original x64 structure's tail padding. */
    uint32_t reserved_v1_padding;
    /* Optional logical 1920x1080 placement, gated by CUSTOM_POSITION. */
    int32_t x;
    int32_t y;
} San14PanelSpec;
#define SAN14_PANEL_SPEC_V1_SIZE ((uint32_t)offsetof(San14PanelSpec, x))

typedef struct San14ControlSpec {
    uint32_t struct_size;
    San14StringView control_id;
    San14StringView text;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    uint32_t flags;
} San14ControlSpec;

typedef void (SAN14_MOD_CALL *San14ControlCallback)(
    void* plugin_context,
    San14ControlHandle control
);

typedef struct San14OfficerActionSpec {
    uint32_t struct_size;
    San14StringView action_id;
    San14StringView label;
    int32_t priority;
    San14ControlCallback callback;
    void* plugin_context;
} San14OfficerActionSpec;

typedef struct San14UiApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *create_panel)(
        void* host_context,
        const San14PanelSpec* spec,
        San14PanelHandle* out_panel
    );
    San14Result (SAN14_MOD_CALL *add_label)(
        void* host_context,
        San14PanelHandle panel,
        const San14ControlSpec* spec,
        San14ControlHandle* out_control
    );
    San14Result (SAN14_MOD_CALL *add_button)(
        void* host_context,
        San14PanelHandle panel,
        const San14ControlSpec* spec,
        San14ControlCallback callback,
        void* plugin_context,
        San14ControlHandle* out_control
    );
    San14Result (SAN14_MOD_CALL *set_text)(
        void* host_context,
        San14ControlHandle control,
        San14StringView text
    );
    San14Result (SAN14_MOD_CALL *show_panel)(void* host_context, San14PanelHandle panel);
    San14Result (SAN14_MOD_CALL *close_panel)(void* host_context, San14PanelHandle panel);
    San14Result (SAN14_MOD_CALL *register_officer_action)(
        void* host_context,
        const San14OfficerActionSpec* spec,
        San14Handle* out_action
    );
    San14Result (SAN14_MOD_CALL *remove_officer_action)(
        void* host_context,
        San14Handle action
    );
} San14UiApi;

#endif
