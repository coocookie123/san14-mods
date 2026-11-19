#ifndef SAN14_MOD_SDK_ROSTER_H
#define SAN14_MOD_SDK_ROSTER_H
#include "san14_types.h"

/* Copied, read-only roster values. A capture ID identifies this result only;
 * it is not authorization to submit a game command or a native list selection. */
enum San14RosterField {
    SAN14_ROSTER_NAMES=1, SAN14_ROSTER_BASE_STATS=2, SAN14_ROSTER_LOYALTY=4,
    SAN14_ROSTER_TRAITS=8, SAN14_ROSTER_FORMATIONS=16,
    SAN14_ROSTER_OWNERSHIP=32
};
/* Objective assignment facts. Categorization and filter policy belong to plugins. */
enum San14AssignmentFact {
    SAN14_ASSIGNMENT_LOCATION=1, SAN14_ASSIGNMENT_CITY_ROLES=2,
    SAN14_ASSIGNMENT_NATIVE_TEXTS=4, SAN14_ASSIGNMENT_REGION_RAW=8
};
typedef struct San14NamedId {uint32_t id;char name_utf8[64];} San14NamedId;
typedef struct San14RosterOfficer {
    uint32_t struct_size;
    uint32_t officer_id;
    uint64_t available_fields;
    char name_utf8[128];
    char courtesy_utf8[64];
    char phonetic_utf8[384];
    int32_t base_stats[5];
    int32_t loyalty_raw;
    int32_t reserved; /* zero; no plugin-specific duty categorization in Base. */
    uint32_t rank_code;
    uint32_t district_id;
    uint32_t force_id;
    uint32_t home_location_id;
    uint32_t current_location_id;
    uint32_t trait_count;
    San14NamedId traits[5];
    uint32_t formation_count;
    San14NamedId formations[21];
    uint32_t assignment_facts;
    uint32_t city_recruiter;
    uint32_t city_trainer;
    int32_t region_assignment_raw;
    char primary_assignment_utf8[256];
    char secondary_assignment_utf8[256];
    /* Optional tail; traits[5]/trait_count above keep their original meaning.
       Slot positions are preserved, including empty and unresolved IDs.
       The valid mask identifies slots with a resolved, nonzero named ID;
       effective_trait_slots is separate from native effect activation bits. */
    uint32_t physical_trait_slots;
    uint32_t effective_trait_slots;
    uint32_t valid_trait_slot_mask;
    San14NamedId trait_slots[9];
} San14RosterOfficer;
typedef struct San14RosterInfo {
    uint32_t struct_size;
    uint32_t officer_count;
    uint64_t capture_id;
    uint32_t player_force_id;
    uint32_t reserved;
    char environment_id[96];
    uint32_t assignment_labels_available;
    char city_recruit_label_utf8[64];
    char city_train_label_utf8[64];
} San14RosterInfo;
typedef struct San14RosterApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *capture)(void* host_context,San14RosterInfo* info,
        void* officers,uint32_t record_stride,uint32_t capacity,uint32_t* required);
} San14RosterApi;
/* Frozen minimum prefixes. Tail fields are present only when struct_size covers
 * them. Each record occupies caller-supplied record_stride bytes; capture copies
 * at most that many bytes and reports the written prefix in struct_size.
 * Services embedded in HostApi are frozen; future service versions use a new
 * interface rather than growing an embedded table and moving later services. */
#define SAN14_ROSTER_OFFICER_V1_MIN_SIZE ((uint32_t)offsetof(San14RosterOfficer,assignment_facts))
#define SAN14_ROSTER_INFO_V1_MIN_SIZE ((uint32_t)offsetof(San14RosterInfo,assignment_labels_available))
#endif
