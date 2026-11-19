#ifndef SAN14_MOD_SDK_DOMESTIC_H
#define SAN14_MOD_SDK_DOMESTIC_H
#include "san14_types.h"
/* Actual positive domestic settlement deltas since recording was enabled.
 * Shared campaign counters, restored with save attachments. No retrospective
 * reconstruction. Development uses native points, not gold/food income.
 * query is a read/data-context operation; set_recording is UI-thread only.
 * Multiple owners share recording; disabling one releases only its lease. */
typedef struct San14DomesticTotals {
    uint32_t struct_size;
    uint32_t officer_id;
    uint64_t commerce,agriculture,barracks,recruited,morale;
} San14DomesticTotals;
typedef struct San14DomesticApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *set_recording)(void*,int32_t enabled);
    San14Result (SAN14_MOD_CALL *query)(void*,uint32_t officer_id,San14DomesticTotals*);
} San14DomesticApi;
#endif
