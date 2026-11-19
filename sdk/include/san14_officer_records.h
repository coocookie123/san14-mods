#ifndef SAN14_MOD_SDK_OFFICER_RECORDS_H
#define SAN14_MOD_SDK_OFFICER_RECORDS_H
#include "san14_types.h"
#define SAN14_RECORD_DUEL 1u
#define SAN14_RECORD_UNITS 2u
#define SAN14_RECORD_CITIES 4u
#define SAN14_RECORD_CAPTURES 8u
#define SAN14_RECORD_EMPLOY 16u
#define SAN14_RECORD_DIPLOMACY 32u
#define SAN14_RECORD_BATTLE 15u
#define SAN14_RECORD_DOMESTIC 48u
#define SAN14_RECORD_ALL 63u
/* Confirmed outcomes since recording began, shared by campaign and restored
 * with save attachments. Draws add neither a win nor a loss. No casualty data.
 * A missing stored record means zero; unavailable adapter categories are
 * explicitly reported by available_categories rather than invented as zero. */
typedef struct San14OfficerRecords {
    uint32_t struct_size,officer_id;
    uint32_t available_categories,reserved;
    uint64_t duel_wins,duel_losses,units_destroyed,units_lost,cities_taken;
    uint64_t officers_captured,times_captured,employ_success,diplomacy_success;
} San14OfficerRecords;
typedef struct San14OfficerRecordsApi {
    uint32_t struct_size;
    /* UI-thread recording lease; mask=0 releases this owner's whole lease.
     * Requires game.read.officers and storage.playthrough, as does query. */
    San14Result (SAN14_MOD_CALL *set_recording)(void*,uint32_t categories);
    /* UI or Host roster-refresh data context. Initialize output struct_size. */
    San14Result (SAN14_MOD_CALL *query)(void*,uint32_t officer_id,San14OfficerRecords*);
    San14Result (SAN14_MOD_CALL *capabilities)(void*,uint32_t* categories);
} San14OfficerRecordsApi;
#endif
