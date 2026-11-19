#ifndef SAN14_MOD_SDK_OFFICER_TRAITS_H
#define SAN14_MOD_SDK_OFFICER_TRAITS_H
#include "san14_types.h"
enum San14OfficerTraitsFlag {
    SAN14_TRAITS_CAN_CONFIGURE=1,
    SAN14_TRAITS_CAN_EDIT=2,
    SAN14_TRAITS_DEPLOYED=4
};
typedef struct San14OfficerTraitsStatus {
    uint32_t struct_size,active_slots,physical_slots,flags;
    uint64_t generation;
} San14OfficerTraitsStatus;
typedef struct San14OfficerTraits {
    uint32_t struct_size,officer_id;
    uint64_t generation;
    uint32_t active_slots,active_mask,flags;
    uint16_t ids[9]; /* 0 means no trait. */
    uint16_t reserved;
} San14OfficerTraits;
typedef struct San14OfficerTraitOption {
    uint32_t struct_size,id;
    char name_utf8[128];
} San14OfficerTraitOption;
typedef struct San14OfficerExtraTraitsEdit {
    uint32_t struct_size,officer_id;
    uint64_t generation;
    uint16_t ids[4];
} San14OfficerExtraTraitsEdit;
/* Optional semantic service. All calls are UI-thread only. Reading requires
   game.read.officers; changing the engine limit requires game.rules; editing
   additionally requires game.commands. Enable/disable only at the main menu,
   before loading a save: native five-slot recalculation can erase extra IDs.
   Editing requires nine-slot mode and an officer who is not deployed. */
typedef struct San14OfficerTraitsApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *query)(void*,San14OfficerTraitsStatus*);
    San14Result (SAN14_MOD_CALL *set_expanded)(void*,int32_t enabled);
    San14Result (SAN14_MOD_CALL *read)(void*,uint32_t officer_id,San14OfficerTraits*);
    San14Result (SAN14_MOD_CALL *options)(void*,void* output,uint32_t stride,uint32_t capacity,uint32_t* required);
    San14Result (SAN14_MOD_CALL *replace_extra)(void*,const San14OfficerExtraTraitsEdit*);
} San14OfficerTraitsApi;
#endif
