#ifndef SAN14_MOD_SDK_UI_LOG_H
#define SAN14_MOD_SDK_UI_LOG_H
#include "san14_ui.h"
/* Read-only intelligence-style log. UTF-8 text; ^00 body, ^02 person/unit,
 * ^05 force/place. No links, callbacks, native pointers or navigation.
 * Strings are copied before return. One log per panel; set before show.
 * Requires ui.panel, UI thread. Host owns scrolling and native resources. */
typedef struct San14LogRow {
    uint32_t struct_size;
    uint32_t year,month,day;
    /* Native palette index 0..61; UINT32_MAX hides unknown/no-force badge. */
    uint32_t force_color;
    San14StringView banner,text;
} San14LogRow;
typedef struct San14UiLogApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *set_rows)(void*,San14PanelHandle,
        const San14LogRow*,uint32_t count,uint32_t stride);
} San14UiLogApi;
#endif
