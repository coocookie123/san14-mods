#ifndef SAN14_MOD_SDK_IDLE_H
#define SAN14_MOD_SDK_IDLE_H

#include "san14_events.h"

/* UI message-loop idle is NOT a turn boundary or a game mutation safe point.
 * San14EventView.generation is zero for this event. Its payload points to a
 * San14IdleInfo copied by the Host and is valid only for the callback duration.
 * Callbacks must cooperate with the suggested remaining budget; Host cannot
 * preempt a plugin that blocks inside a callback. */
#ifdef __cplusplus
inline constexpr uint32_t SAN14_EVENT_UI_IDLE = 0x0003u;
#else
#define SAN14_EVENT_UI_IDLE UINT32_C(0x0003)
#endif

typedef struct San14IdleInfo {
    uint32_t struct_size;
    uint64_t monotonic_ms;
    uint32_t suggested_budget_us;
} San14IdleInfo;

#endif
