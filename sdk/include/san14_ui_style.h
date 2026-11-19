#ifndef SAN14_MOD_SDK_UI_STYLE_H
#define SAN14_MOD_SDK_UI_STYLE_H
#include "san14_ui.h"

/* Optional service at the END of San14HostApi. Never extend an embedded table. */
#define SAN14_UI_STYLE_EXPERT 1u
#define SAN14_UI_STYLE_TINT 2u
#define SAN14_UI_STYLE_ENABLED 4u
/* set_tint also accepts native button captions when this capability is present. */
#define SAN14_UI_STYLE_BUTTON_TEXT_TINT 8u
#define SAN14_PANEL_EXPERT 2u
#define SAN14_CONTROL_EXPERT_ROW 2u
#define SAN14_CONTROL_TAB 4u
#define SAN14_CONTROL_CHOICE_LEFT 8u
#define SAN14_CONTROL_CHOICE_RIGHT 16u
#define SAN14_CONTROL_CONFIRM 32u
#define SAN14_CONTROL_SELECTED 64u
#define SAN14_CONTROL_EXPERT_LABEL 128u
/* Read-only stock chronology row, width 1112, height 58..240 logical pixels. Text is
 * year TAB month TAB day TAB description; empty text clears all four fields. */
#define SAN14_CONTROL_CHRONICLE_ROW 256u
/* Text-only clickable control, no button frame or padding. */
#define SAN14_CONTROL_LINK 512u

/* Shared 1920 x 1080 layout used by the manager and the SDK page builder. */
#define SAN14_EXPERT_WIDTH 1920
#define SAN14_EXPERT_HEIGHT 1080
#define SAN14_EXPERT_LEFT 360
#define SAN14_EXPERT_TOP 174
#define SAN14_EXPERT_ROW_WIDTH 1200
#define SAN14_EXPERT_ROW_HEIGHT 54
#define SAN14_EXPERT_ROW_PITCH 66
#define SAN14_EXPERT_ROWS 8
#define SAN14_EXPERT_HELP_TOP 758
#define SAN14_EXPERT_HELP_HEIGHT 132
#define SAN14_EXPERT_FOOTER_TOP 944

typedef struct San14UiStyleApi {
    uint32_t struct_size;
    uint32_t capabilities;
    /* Radio/tab visual state only; the page owns mutual exclusion. */
    San14Result (SAN14_MOD_CALL *set_selected)(void* host_context, San14ControlHandle control, int32_t selected);
    /* Copy UTF-8 help; source and label must belong to the same owner AND panel.
     * A zero label removes the binding. Hovering labels also displays help. */
    San14Result (SAN14_MOD_CALL *bind_help)(void* host_context, San14ControlHandle source,
                                         San14ControlHandle label, San14StringView description);
    /* Tint an existing label control in 0xAARRGGBB format (e.g. 0xFFRRGGBB). */
    San14Result (SAN14_MOD_CALL *set_tint)(void* host_context, San14ControlHandle control, uint32_t argb);
    /* Optional tail, gated by ENABLED and struct_size. Stock rectangular styled
     * buttons only; disabled buttons use native grey styling and reject mouse
     * and keyboard activation. Does not change selected state or invoke callbacks. */
    San14Result (SAN14_MOD_CALL *set_enabled)(void* host_context, San14ControlHandle control, int32_t enabled);
} San14UiStyleApi;

/* Generalized native slider control for custom plugin panels.
 * Optional tail service via San14HostApi.ui_slider. */
typedef void (SAN14_MOD_CALL *San14IntChangedCallback)(
    void* plugin_context,
    San14ControlHandle control,
    int32_t value
);

typedef struct San14SliderSpec {
    uint32_t struct_size;
    San14StringView control_id;
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
    int32_t min_value;
    int32_t max_value;
    int32_t step;
    int32_t value;
    int32_t display_divisor;
    uint32_t flags;
    San14StringView unit_suffix;
} San14SliderSpec;

typedef struct San14UiSliderApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *add_slider)(
        void* host_context,
        San14PanelHandle panel,
        const San14SliderSpec* spec,
        San14IntChangedCallback changed,
        void* plugin_context,
        San14ControlHandle* out_control
    );
    /* Updates the slider value visually without invoking changed, avoiding recursion. */
    San14Result (SAN14_MOD_CALL *set_slider_value)(
        void* host_context,
        San14ControlHandle control,
        int32_t value
    );
    San14Result (SAN14_MOD_CALL *get_slider_value)(
        void* host_context,
        San14ControlHandle control,
        int32_t* out_value
    );
} San14UiSliderApi;
#endif
