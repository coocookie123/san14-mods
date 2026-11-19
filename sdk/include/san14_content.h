#ifndef SAN14_MOD_SDK_CONTENT_H
#define SAN14_MOD_SDK_CONTENT_H
#include "san14_types.h"

/* Experimental native data packs. Mount/unmount only at the title menu;
   game.content permission. Host owns parsing, native slots, hooks and rollback.
   Only the capabilities reported by the current adapter may be used. */
enum San14ContentCapability {
    SAN14_CONTENT_NATIVE_RECORDS = 1,
    SAN14_CONTENT_RESERVED_OFFICER = 2,
    SAN14_CONTENT_DERIVED_TRAIT = 4,
    SAN14_CONTENT_BUILTIN_PORTRAITS = 8,
    SAN14_CONTENT_SCENARIO_REPLACEMENT = 16,
    SAN14_CONTENT_DECLARATIVE_RECORDS = 32,
    SAN14_CONTENT_BUNDLED_PORTRAITS = 64,
    SAN14_CONTENT_DUEL_CAMPAIGN = 128,
    /* schema v2 scenario.native_file: an attested, bundled native .s14 file.
       The engine reads it before rebuilding its own IDs and force lists. */
    SAN14_CONTENT_NATIVE_SCENARIO_FILE = 256,
    /* schema v2 campaign court & zero-prefecture rules: ruler_title/title,
       imperial regime, office_assignment="max", and prefecture_defaults.count=0. */
    SAN14_CONTENT_CAMPAIGN_COURT = 512
};
enum San14ContentRecordKind { SAN14_CONTENT_OFFICER=1, SAN14_CONTENT_TRAIT=2, SAN14_CONTENT_PORTRAIT=3 };
enum San14ContentPhase {
    SAN14_CONTENT_UNAVAILABLE = 0,
    SAN14_CONTENT_TITLE = 1,
    SAN14_CONTENT_WORLD = 2,
    SAN14_CONTENT_TRANSITION = 3
};
typedef struct San14ContentStatus {
    uint32_t struct_size;
    uint32_t capabilities;
    uint32_t phase;
    uint32_t mounted;
    uint32_t applied;
    uint32_t added_officer_id;
    uint32_t added_trait_id;
    int32_t last_result;
    char message_utf8[256];
} San14ContentStatus;
typedef struct San14ContentApi {
    uint32_t struct_size;
    /* Path is relative to this plugin's root, UTF-8, no traversal/reparse points.
       A complete pack is copied and validated before registration. Exclusive
       ownership; another mounted pack returns BUSY. No original resource files
       are overwritten. Schema v2 scenario.base_id (v1 scenario_entry) selects a native scenario ID to replace
       in the selection screen; only that selection uses the variant. Packs
       without scenario_entry retain the legacy all-new-games behavior. Old saves
       are not converted. Matching sidecars restore the variant on load. */
    San14Result (SAN14_MOD_CALL *mount)(void*, San14StringView relative_path);
    /* Returns BUSY outside the title menu, retaining ownership and all data. */
    San14Result (SAN14_MOD_CALL *unmount)(void*);
    San14Result (SAN14_MOD_CALL *status)(void*, San14ContentStatus*);
    /* Resolve this pack's stable local key after campaign application. Native
       IDs are Host allocated and persisted with the save, never addresses. */
    San14Result (SAN14_MOD_CALL *resolve)(void*, uint32_t kind, San14StringView local_key, uint32_t* native_id);
} San14ContentApi;
#endif
