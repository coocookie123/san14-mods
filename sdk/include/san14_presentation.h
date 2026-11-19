#ifndef SAN14_MOD_SDK_PRESENTATION_H
#define SAN14_MOD_SDK_PRESENTATION_H
#include "san14_types.h"
/* UI thread, active owner, game.rules permission. Queues the game's current
 * turn report; returns BUSY when another request is pending. The next strategy
 * update checks world/player/foreground eligibility before consuming it.
 * Successful queuing does not guarantee a window opens. Never advances time. */
typedef struct San14PresentationApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *request_turn_report)(void* host_context);
} San14PresentationApi;
#endif
