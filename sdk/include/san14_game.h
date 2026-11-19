#ifndef SAN14_MOD_SDK_GAME_H
#define SAN14_MOD_SDK_GAME_H

#include "san14_types.h"

typedef struct San14OfficerSnapshot {
    uint32_t struct_size;
    uint32_t officer_id;
    uint64_t generation;
    char name_utf8[64];
    int32_t leadership;
    int32_t strength;
    int32_t intelligence;
    int32_t politics;
    int32_t charisma;
    uint32_t force_id;
    uint32_t city_id;
    int32_t loyalty;
    uint32_t status;
} San14OfficerSnapshot;

typedef struct San14GameApi {
    uint32_t struct_size;
    uint32_t game_build;
    San14Result (SAN14_MOD_CALL *current_generation)(
        void* host_context,
        uint64_t* out_generation
    );
    San14Result (SAN14_MOD_CALL *get_officer)(
        void* host_context,
        uint32_t officer_id,
        San14OfficerSnapshot* out_snapshot
    );
    San14Result (SAN14_MOD_CALL *submit_command)(
        void* host_context,
        uint32_t command_id,
        const void* parameters,
        uint32_t parameter_size,
        San14CommandHandle* out_command
    );
} San14GameApi;

#endif
