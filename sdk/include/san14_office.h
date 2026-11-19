#ifndef SAN14_MOD_SDK_OFFICE_H
#define SAN14_MOD_SDK_OFFICE_H
#include "san14_types.h"

typedef enum San14OfficeMode {
    SAN14_OFFICE_COMPREHENSIVE = 0,
    SAN14_OFFICE_MILITARY = 1,
    SAN14_OFFICE_CIVIL = 2
} San14OfficeMode;
typedef enum San14OfficeClass {
    SAN14_OFFICE_KEEP_CLASS = -1,
    SAN14_OFFICE_CLASS_CIVIL = 0,
    SAN14_OFFICE_CLASS_MILITARY = 1
} San14OfficeClass;

/* Copied base attributes; no game object or address crosses this service.
 * id is 1..6000; attributes are 0..100; merit preserves the signed native value. */
typedef struct San14OfficeCandidate {
    uint32_t struct_size;
    uint32_t id;
    int32_t leadership, martial, intelligence, politics, merit;
} San14OfficeCandidate;

/* Callbacks are synchronous on the game's calling thread. Inputs are borrowed
 * only for the callback. Keep them fast; do not retain pointers, call Host APIs,
 * block, throw, or mutate game state. Return non-OK to preserve the current
 * native operation. Ties use officer ID ascending, then original order.
 * max_merit is the greatest nonnegative candidate merit, with a minimum of 1. */
typedef San14Result (SAN14_MOD_CALL *San14OfficeScore)(
    void* plugin_context, const San14OfficeCandidate* candidate,
    int32_t max_merit, San14OfficeMode mode, int64_t* out_score);
typedef San14Result (SAN14_MOD_CALL *San14OfficeClassifyOfficer)(
    void* plugin_context, const San14OfficeCandidate* candidate,
    San14OfficeClass* out_class);
typedef San14Result (SAN14_MOD_CALL *San14OfficeClassifyPost)(
    void* plugin_context, uint32_t post_id, uint32_t stat_index,
    const int32_t native_bonuses[4], int32_t* out_bonus);

typedef struct San14OfficePolicy {
    uint32_t struct_size;
    void* plugin_context;
    San14OfficeScore score;
    San14OfficeClassifyOfficer classify_officer;
    San14OfficeClassifyPost classify_post;
} San14OfficePolicy;

/* Optional service, permission game.rules. Only one active plugin can
 * register a policy. Registration copies the table; the context remains owned
 * by the plugin. Register during on_enable, before enabling the named rule
 * officers.office.weighted. Hot disable drops the policy before DLL cleanup.
 * The rule uses a comprehensive candidate list and a second native assignment
 * pass. The Host also raises the merit cap while the rule is enabled.
 * All three callbacks are required. NULL unregisters this owner's policy and
 * disables the rule. Another owner's registration returns SAN14_E_BUSY. */
typedef struct San14OfficeApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *register_policy)(
        void* host_context, const San14OfficePolicy* policy);
} San14OfficeApi;

#endif
