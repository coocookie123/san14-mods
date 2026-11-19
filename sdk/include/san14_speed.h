#ifndef SAN14_MOD_SDK_SPEED_H
#define SAN14_MOD_SDK_SPEED_H

#include "san14_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Keep startup, intro/title menus, and loading screens at 1.00x and engage
 * the configured speed multiplier only while an active game map is running. */
#define SAN14_SPEED_FLAG_IN_GAME_ONLY 1u

typedef struct San14SpeedCaps {
    uint32_t struct_size;
    uint32_t min_speed_x100;
    uint32_t max_speed_x100;
    uint32_t default_step_x100;
    uint32_t supported;
} San14SpeedCaps;

typedef struct San14GameSpeedApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *query_caps)(
        void* host_context,
        San14SpeedCaps* out_caps
    );
    /* Configures the caller's desired speed in hundredths (100 = 1.00x, 800 = 8.00x).
     * Requires the "game.rules" permission and UI thread. Exclusive to one active owner
     * while speed != 100; setting 100 releases ownership. Host resets to 100 when the
     * owning Mod is disabled or unloaded. */
    San14Result (SAN14_MOD_CALL *set_speed_x100)(
        void* host_context,
        uint32_t speed_x100,
        uint32_t flags
    );
    /* Reads the configured desired speed (x100) and the currently active clock multiplier. */
    San14Result (SAN14_MOD_CALL *get_speed_x100)(
        void* host_context,
        uint32_t* out_configured_x100,
        uint32_t* out_effective_x100
    );
} San14GameSpeedApi;

#ifdef __cplusplus
}
#endif

#endif
