#ifndef SAN14_MOD_SDK_NAVIGATION_H
#define SAN14_MOD_SDK_NAVIGATION_H
#include "san14_types.h"
enum San14DetailKind {SAN14_DETAIL_OFFICER=1,SAN14_DETAIL_CITY=2};
/* Native read-only details, no game pointers. UI thread; close plugin modals
 * before opening. Needs ui.panel plus game.read.officers / game.read.cities.
 * The native dialog owns its lifetime; releasing a ticket does not close it. */
typedef struct San14NavigationApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *open_details)(void*,uint32_t kind,uint32_t id,San14Handle* ticket);
    /* Reports closure once and releases the ticket. A campaign revision change
     * returns STALE_GENERATION and releases it. */
    San14Result (SAN14_MOD_CALL *is_open)(void*,San14Handle ticket,int32_t* open);
    San14Result (SAN14_MOD_CALL *release)(void*,San14Handle ticket);
} San14NavigationApi;
#endif
