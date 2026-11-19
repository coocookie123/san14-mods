#ifndef SAN14_MOD_SDK_FORCE_COLOR_H
#define SAN14_MOD_SDK_FORCE_COLOR_H

#include "san14_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    SAN14_FORCE_COLOR_PALETTE_SIZE = 60,
    SAN14_FORCE_COLOR_MAX_FORCES = 50,
    SAN14_FORCE_COLOR_TRIBAL_MIN = 46,
    SAN14_FORCE_COLOR_TRIBAL_MAX = 50,
};

typedef struct San14ForceColorSwatch {
    uint32_t struct_size;
    uint8_t color_id;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t alpha;
    uint8_t reserved[3];
} San14ForceColorSwatch;

typedef struct San14ForceColorEntry {
    uint32_t struct_size;
    uint32_t force_id;
    uint32_t ruler_id;
    uint8_t color_id;
    uint8_t is_tribal;
    uint8_t reserved[2];
} San14ForceColorEntry;

typedef void (SAN14_MOD_CALL *San14ForceActionCallback)(void* context, uint32_t force_id);
typedef struct San14ForceDetailActionSpec {
    uint32_t struct_size;
    San14StringView label;
    San14StringView button_text;
    San14ForceActionCallback callback;
    void* plugin_context;
} San14ForceDetailActionSpec;

typedef struct San14ForceColorApi {
    uint32_t struct_size;
    /* Read the 60 built-in palette swatches (color_id 0..59). Requires "game.read.forces". */
    San14Result (SAN14_MOD_CALL *read_palette)(
        void* host_context,
        San14ForceColorSwatch* out_swatches,
        uint32_t capacity,
        uint32_t* out_count
    );
    /* List active regular and tribal forces (force_id 1..50). Requires "game.read.forces". */
    San14Result (SAN14_MOD_CALL *list_forces)(
        void* host_context,
        San14ForceColorEntry* out_forces,
        uint32_t capacity,
        uint32_t* out_count
    );
    /* Change force_id's color (0..59) and refresh the strategic map.
       Returns SAN14_E_BUSY if another active force already occupies color_id.
       Optionally writes the previous color ID to out_previous_color_id.
       Requires UI thread and "game.commands". */
    San14Result (SAN14_MOD_CALL *set_color)(
        void* host_context,
        uint32_t force_id,
        uint8_t color_id,
        uint8_t* out_previous_color_id
    );
    /* Reserved preview-v2 tail. Current Host advertises V1_SIZE and leaves
       these pointers NULL; native Edit Force actions have been removed.
       Retained declarations preserve compatibility with existing SDK sources. */
    San14Result (SAN14_MOD_CALL *register_force_detail_action)(void*, const San14ForceDetailActionSpec*);
    San14Result (SAN14_MOD_CALL *unregister_force_detail_action)(void*);
    /* Reserved with the preview-v2 action functions above. */
    San14Result (SAN14_MOD_CALL *query_inspected_edit_force)(void*, uint32_t* out_force_id);
} San14ForceColorApi;

#define SAN14_FORCE_COLOR_API_V1_SIZE ((uint32_t)offsetof(San14ForceColorApi, register_force_detail_action))
#define SAN14_FORCE_COLOR_API_V2_SIZE ((uint32_t)sizeof(San14ForceColorApi))

#ifdef __cplusplus
}
#endif

#endif
