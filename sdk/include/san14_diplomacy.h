#ifndef SAN14_MOD_SDK_DIPLOMACY_H
#define SAN14_MOD_SDK_DIPLOMACY_H

#include "san14_types.h"

#ifdef __cplusplus
extern "C" {
#endif

enum { SAN14_DIPLOMACY_MATRIX_STRIDE = 55 };

typedef struct San14DiplomacyForce {
    uint32_t struct_size;
    uint32_t force_id;
    uint32_t ruler_id;
    uint32_t city_count;
    int32_t troop_count;
    uint32_t officer_count;
    int32_t diplomacy_power;
    uint8_t alive;
    uint8_t is_player;
    uint8_t reserved[2];
} San14DiplomacyForce;

/* relations and treaties are flat arrays indexed as [force_a * matrix_stride + force_b]
 * for 1 <= force_a, force_b < matrix_stride. treaties holds the raw 7-bit treaty flags
 * (treaties[...] & 0x7f != 0 indicates an active treaty). */
typedef struct San14DiplomacySnapshot {
    uint32_t struct_size;
    uint32_t year;
    uint32_t month;
    uint32_t day;
    int32_t total_days;
    int32_t elapsed_days;
    uint32_t player_force_id;
    uint32_t coalition_target_force_id;
    const San14DiplomacyForce* forces;
    uint32_t force_count;
    const uint8_t* relations;
    const uint8_t* treaties;
    uint32_t matrix_stride;
} San14DiplomacySnapshot;

typedef void (SAN14_MOD_CALL *San14DiplomacyResetCallback)(
    void* plugin_context
);

typedef void (SAN14_MOD_CALL *San14DiplomacyTickCallback)(
    void* plugin_context,
    const San14DiplomacySnapshot* snapshot
);

/* Return SAN14_OK with *out_value set to override, or non-OK to keep native_value. */
typedef San14Result (SAN14_MOD_CALL *San14DiplomacyValueQuery)(
    void* plugin_context,
    const San14DiplomacySnapshot* snapshot,
    uint32_t force_a,
    uint32_t force_b,
    int32_t native_value,
    int32_t* out_value
);

/* Set *out_block != 0 and return SAN14_OK to suppress an AI alliance annulment. */
typedef San14Result (SAN14_MOD_CALL *San14DiplomacyBlockAnnulQuery)(
    void* plugin_context,
    const San14DiplomacySnapshot* snapshot,
    uint32_t force_a,
    uint32_t force_b,
    int32_t* out_block
);

typedef struct San14DiplomacyPolicy {
    uint32_t struct_size;
    void* plugin_context;
    San14DiplomacyResetCallback on_reset;
    San14DiplomacyTickCallback on_tick;
    San14DiplomacyValueQuery on_relation_query;
    San14DiplomacyValueQuery on_acceptance_query;
    San14DiplomacyValueQuery on_cost_query;
    San14DiplomacyValueQuery on_duration_query;
    San14DiplomacyBlockAnnulQuery on_block_annul_query;
} San14DiplomacyPolicy;

/* Optional service for AI diplomacy overrides and state inspection.
 * Requires permission game.rules for policy registration (single active owner),
 * paired with named rule "diplomacy.ai.dynamic". */
typedef struct San14DiplomacyApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *register_policy)(
        void* host_context,
        const San14DiplomacyPolicy* policy
    );
    San14Result (SAN14_MOD_CALL *unregister_policy)(
        void* host_context
    );
} San14DiplomacyApi;

#ifdef __cplusplus
}
#endif

#endif
