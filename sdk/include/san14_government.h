#ifndef SAN14_GOVERNMENT_H
#define SAN14_GOVERNMENT_H
#include "san14_types.h"

#define SAN14_GOVERNMENT_MAX_CITIES 51u
#define SAN14_GOVERNMENT_MAX_UNITS 500u
#define SAN14_GOVERNMENT_MAX_POSTS 35u
#define SAN14_GOVERNMENT_MAX_POLICIES 120u

// Copied game values, never engine pointers. Terrain samples cover radius 14
// around each owned city's foothold, with the engine's hex distance metric.
typedef struct San14GovernmentCity {
    int32_t city_id,force_id,tile_id,troops,food,gold;
    uint32_t sampled_tiles;
    uint16_t terrain_counts[256];
} San14GovernmentCity;
typedef struct San14GovernmentUnit {
    int32_t force_id,tile_id,troops,terrain_id;
} San14GovernmentUnit;
typedef struct San14GovernmentSnapshot {
    uint32_t struct_size,ready;
    uint64_t snapshot_id;
    int32_t force_id,orders,advisor_id,advisor_policy_id,advisor_policy_level;
    uint32_t city_count,unit_count,current_officer_count;
    uint8_t enemy_forces[52];
    uint16_t current_officer_ids[SAN14_GOVERNMENT_MAX_POSTS];
    uint8_t current_policy_levels[SAN14_GOVERNMENT_MAX_POLICIES+1];
    uint8_t current_naval_level;
    San14GovernmentCity cities[SAN14_GOVERNMENT_MAX_CITIES];
    San14GovernmentUnit units[SAN14_GOVERNMENT_MAX_UNITS];
} San14GovernmentSnapshot;
typedef struct San14GovernmentCandidate {
    uint32_t struct_size;
    int32_t officer_id,policy_id,effective_level,cost;
    uint32_t current;
} San14GovernmentCandidate;

// Synchronous game-thread callback. Inputs are borrowed only during this call.
// Return SAN14_OK and a score; higher scores are considered first. Do not call
// host APIs or retain these input pointers. No memory addresses cross this API.
typedef San14Result (SAN14_MOD_CALL *San14GovernmentScoreCallback)(
    void* plugin_context,const San14GovernmentSnapshot* snapshot,
    const San14GovernmentCandidate* candidate,int32_t* score);
typedef struct San14GovernmentApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *register_policy)(void* host_context,
        San14GovernmentScoreCallback callback,void* plugin_context);
    San14Result (SAN14_MOD_CALL *unregister_policy)(void* host_context);
} San14GovernmentApi;
#endif
