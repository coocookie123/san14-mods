#ifndef SAN14_MOD_SDK_OFFICER_EDIT_H
#define SAN14_MOD_SDK_OFFICER_EDIT_H
#include "san14_types.h"

#define SAN14_OFFICER_EDIT_CAN_APPLY 1u
#define SAN14_OFFICER_EDIT_ALL_STATS 31u
/* Order: leadership, war, intelligence, politics, charisma. Base values only. */
typedef struct San14OfficerEditState {
    uint32_t struct_size,officer_id;
    uint64_t generation;
    uint32_t flags;
    uint32_t base_stats[5],experience[5];
} San14OfficerEditState;
typedef struct San14OfficerStatEdit {
    uint32_t struct_size,officer_id;
    uint64_t generation;
    uint32_t field_mask;
    uint32_t expected_stats[5],expected_experience[5],base_stats[5];
} San14OfficerStatEdit;
/* Exact undo: restores a previously read stat/experience pair. Selected target
   experience must belong to the target ability's native threshold interval. */
typedef struct San14OfficerStatRestore {
    uint32_t struct_size,officer_id;
    uint64_t generation;
    uint32_t field_mask;
    uint32_t expected_stats[5],expected_experience[5],base_stats[5],experience[5];
} San14OfficerStatRestore;
/* UI-thread only. Read requires game.read.officers; validate/apply/restore also
   require game.commands. No game.rules permission or nine-trait mode needed.
   Apply is one transaction, selected base values 1..100, corresponding experience
   reset to the native starting threshold. Unchanged base values preserve their
   existing experience. Other fields remain unchanged.
   Refuses loading, wrong generation, changed expected values and officers away,
   deployed, dead or otherwise outside the supported city identity. Re-read before
   retrying a rejected operation. No automatic save: use the game's normal save.
   validate performs the same checks without writing; no reservation is created.
   Outputs are full post-state only on success; an output-copy error after commit
   must be resolved by re-reading before any retry. */
typedef struct San14OfficerEditApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *read)(void*,uint32_t,San14OfficerEditState*);
    San14Result (SAN14_MOD_CALL *apply)(void*,const San14OfficerStatEdit*,San14OfficerEditState*);
    San14Result (SAN14_MOD_CALL *validate)(void*,const San14OfficerStatEdit*,San14OfficerEditState*);
    San14Result (SAN14_MOD_CALL *restore)(void*,const San14OfficerStatRestore*,San14OfficerEditState*);
} San14OfficerEditApi;
#endif
