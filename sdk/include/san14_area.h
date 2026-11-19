#ifndef SAN14_MOD_SDK_AREA_H
#define SAN14_MOD_SDK_AREA_H

#include "san14_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { SAN14_AREA_MAX_ID = 500 };

typedef enum San14AreaSyncPhase {
    SAN14_AREA_SYNC_PRE_TICK = 1,
    SAN14_AREA_SYNC_POST_SAVE_LOAD = 2,
    SAN14_AREA_SYNC_REVERSE_UPDATE = 3,
    SAN14_AREA_SYNC_WORLD_RESET = 4
} San14AreaSyncPhase;

typedef struct San14AreaSnapshot {
    uint32_t struct_size;
    uint32_t area_id;
    uint32_t city_id;
    uint32_t force_id;
    uint32_t officer_id;
    uint32_t center_tile_id;
    uint32_t aux_mode;
    uint32_t dev_type;
} San14AreaSnapshot;

typedef struct San14AreaOfficerState {
    uint32_t struct_size;
    uint32_t officer_id;
    uint32_t rank;
    uint32_t district_id;
    uint32_t home_city_id;
    uint32_t stay_city_id;
    uint32_t force_id;
    uint8_t is_valid;
    uint8_t is_serving;
    uint8_t is_captive;
    uint8_t is_available;
} San14AreaOfficerState;

typedef struct San14AreaAssignAction {
    uint32_t area_id;
    uint32_t officer_id;
    uint8_t restore_modes;
    uint8_t dev_type;
    uint8_t aux_mode;
    uint8_t reserved;
} San14AreaAssignAction;

typedef struct San14AreaSyncContext {
    uint32_t struct_size;
    San14AreaSyncPhase phase;
    uint32_t player_force_id;
    uint32_t date_key;
    uint32_t event_area_id;
    uint32_t event_new_officer_id;
    uint32_t event_old_officer_id;
    const San14AreaSnapshot* areas;
    uint32_t area_count;
    void* lookup_context;
    San14Result (SAN14_MOD_CALL *lookup_officer)(
        void* lookup_context,
        uint32_t officer_id,
        San14AreaOfficerState* out_state
    );
} San14AreaSyncContext;

typedef San14Result (SAN14_MOD_CALL *San14AreaSyncCallback)(
    void* plugin_context,
    const San14AreaSyncContext* context,
    San14AreaAssignAction* out_actions,
    uint32_t max_actions,
    uint32_t* out_action_count
);

typedef struct San14AreaPolicy {
    uint32_t struct_size;
    void* plugin_context;
    San14AreaSyncCallback on_sync;
} San14AreaPolicy;

/* Optional service for area/prefecture (1..500) state & automated assignment.
 * - Read queries require game.read.areas (or world read context).
 * - Direct assignment requires game.commands.
 * - Policy registration requires game.rules; single active owner at a time,
 *   paired with named rule "area.ai.persistent". */
typedef struct San14AreaApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *list)(
        void* host_context,
        San14AreaSnapshot* out_areas,
        uint32_t capacity,
        uint32_t* out_count
    );
    San14Result (SAN14_MOD_CALL *get)(
        void* host_context,
        uint32_t area_id,
        San14AreaSnapshot* out_area
    );
    San14Result (SAN14_MOD_CALL *get_officer)(
        void* host_context,
        uint32_t officer_id,
        San14AreaOfficerState* out_state
    );
    San14Result (SAN14_MOD_CALL *assign_officer)(
        void* host_context,
        uint32_t area_id,
        uint32_t officer_id
    );
    San14Result (SAN14_MOD_CALL *register_policy)(
        void* host_context,
        const San14AreaPolicy* policy
    );
    San14Result (SAN14_MOD_CALL *unregister_policy)(
        void* host_context
    );
} San14AreaApi;

#ifdef __cplusplus
}
#endif

#endif
