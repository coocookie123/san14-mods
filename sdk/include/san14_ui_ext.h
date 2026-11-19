#ifndef SAN14_MOD_SDK_UI_EXT_H
#define SAN14_MOD_SDK_UI_EXT_H

#include "san14_ui.h"

/* Optional, independently negotiated UI extension. No HWND, IME context or game
 * object is exposed. A string view passed to changed is valid for that callback
 * only. The owner has already accepted the text; get_text observes that value.
 * Programmatic ui.set_text does not fire changed, avoiding callback recursion. */
typedef void (SAN14_MOD_CALL *San14TextChangedCallback)(
    void* plugin_context, San14ControlHandle control, San14StringView value);

/* Positions count Unicode scalar values in committed text, not bytes or UTF-16
 * code units. anchor == caret means an insertion caret without a selection. */
typedef struct San14TextSelection {
    uint32_t struct_size;
    uint32_t anchor;
    uint32_t caret;
} San14TextSelection;

typedef struct San14UiExtendedApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *add_text_input)(
        void* host_context, San14PanelHandle panel, const San14ControlSpec* spec,
        uint32_t max_utf8_bytes, San14TextChangedCallback changed,
        void* plugin_context, San14ControlHandle* out_control);
    /* required_size includes a trailing NUL; capacity is bytes. No partial text
     * is written when capacity is too small. May also read a label or button. */
    San14Result (SAN14_MOD_CALL *get_text)(
        void* host_context, San14ControlHandle control, char* buffer,
        uint32_t capacity, uint32_t* required_size);
    San14Result (SAN14_MOD_CALL *focus_text_input)(
        void* host_context, San14ControlHandle control);
    San14Result (SAN14_MOD_CALL *set_selection)(
        void* host_context, San14ControlHandle control,
        const San14TextSelection* selection);
    San14Result (SAN14_MOD_CALL *get_selection)(
        void* host_context, San14ControlHandle control,
        San14TextSelection* out_selection);
} San14UiExtendedApi;

#endif
