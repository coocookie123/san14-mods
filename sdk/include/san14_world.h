#ifndef SAN14_MOD_SDK_WORLD_H
#define SAN14_MOD_SDK_WORLD_H
#include "san14_types.h"
#include "san14_officer_temperament.h"
#define SAN14_WORLD_CITIES 1u
#define SAN14_WORLD_FORCES 2u
#define SAN14_WORLD_UNITS 4u
#define SAN14_WORLD_ALL 7u
#define SAN14_WORLD_UNKNOWN_ID UINT32_MAX

/* All records are copied values from one immutable capture. No addresses are
 * returned. Two equal bounded reads are required; this is not a paused/atomic
 * game snapshot. Data availability does not imply a command is eligible.
 * Names are NUL-terminated UTF-8; an empty name means unknown/unavailable.
 * Unknown district/force/tile/durability values use SAN14_WORLD_UNKNOWN_ID.
 * ID 0 is preserved where the game represents no assignment. */
typedef struct San14WorldInfo {
    uint32_t struct_size,sections;
    /* captured_tick is monotonic milliseconds (GetTickCount64), not Unix time. */
    uint64_t snapshot_id,generation,captured_tick;
    uint32_t year,month,day,player_force_id;
    uint32_t city_count,force_count,unit_count,reserved;
    char scenario_utf8[96];
} San14WorldInfo;
typedef struct San14CitySnapshot {
    uint32_t struct_size,id,district_id,force_id,governor_id;
    int32_t gold,food,troops;
    uint32_t morale,recruitment_officer_id,training_officer_id;
    uint32_t recorded_gold_expense,recorded_food_expense;
    uint32_t center_tile_id,durability;
    char name_utf8[32];
} San14CitySnapshot;
typedef struct San14ForceSnapshot {
    uint32_t struct_size,id,ruler_id,coalition_member;
    char name_utf8[64];
    uint8_t relations[52],treaties_raw[52];
} San14ForceSnapshot;
typedef struct San14UnitSnapshot {
    uint32_t struct_size,slot,kind_code,commander_id,force_id;
    uint32_t troops,morale,formation_id,tile_id,order_code,target_type,target_id;
    /* Slot range is 0..500 inclusive. Cargo is meaningful only for kind 2;
     * other kinds return SAN14_WORLD_UNKNOWN_ID for both cargo fields. */
    uint32_t waypoint_tiles[5],cargo_gold,cargo_food;
    char commander_name_utf8[64];
} San14UnitSnapshot;
typedef struct San14WorldApi {
    uint32_t struct_size;
    /* UI thread; active owner. Each requested section requires its matching
     * game.read.cities/forces/units permission. Up to 8 captures per owner.
     * Initialize info.struct_size to sizeof(San14WorldInfo). Too-small/invalid
     * outputs return INVALID_ARGUMENT and do not allocate a snapshot handle.
     * The caller's larger structure tail is left untouched. */
    San14Result (SAN14_MOD_CALL *capture)(void*,uint32_t sections,San14Handle*,San14WorldInfo*);
    /* Immutable pages; count is records, stride is the caller's record size.
     * Host copies only the known record prefix and leaves extra tail bytes.
     * Ownership and the section permission are rechecked on every call.
     * A load/reset invalidates old handles (STALE). stride must be at least the
     * current record size; capacity*stride must be <=8 MiB. offset may equal
     * the total count; zero-capacity/end pages succeed and write count=0.
     * Invalid outputs/stride/offset return INVALID_ARGUMENT. Caller owns the
     * buffers and keeps them valid through each call. */
    San14Result (SAN14_MOD_CALL *cities)(void*,San14Handle,uint32_t offset,void*,uint32_t capacity,uint32_t stride,uint32_t* count);
    San14Result (SAN14_MOD_CALL *forces)(void*,San14Handle,uint32_t offset,void*,uint32_t capacity,uint32_t stride,uint32_t* count);
    San14Result (SAN14_MOD_CALL *units)(void*,San14Handle,uint32_t offset,void*,uint32_t capacity,uint32_t stride,uint32_t* count);
    San14Result (SAN14_MOD_CALL *release)(void*,San14Handle);
    San14Result (SAN14_MOD_CALL *current_generation)(void*,uint64_t*);
    /* Optional append-only service. Check struct_size and both pointers. */
    const San14OfficerTemperamentApi* officer_temperament;
} San14WorldApi;
#endif
