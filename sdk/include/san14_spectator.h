#ifndef SAN14_MOD_SDK_SPECTATOR_H
#define SAN14_MOD_SDK_SPECTATOR_H

#include "san14_types.h"

/* Optional exact-build service for the pause-menu force-control chooser.
 * Both manual and spectator modes produce one pending request per click.
 * No game pointer crosses this ABI. */
typedef struct San14SpectatorApi {
    uint32_t struct_size;
    /* UI thread; requires ui.panel, game.read.forces and game.commands.
     * Only one active owner may install the pause-menu integration. */
    San14Result (SAN14_MOD_CALL *set_menu_enabled)(void* host_context, int32_t enabled);
    /* UI thread. Returns 1 once for each intercepted force-control click. */
    San14Result (SAN14_MOD_CALL *poll_menu_request)(void* host_context, int32_t* out_requested);
    /* UI thread. Reports the game's native spectator state. */
    San14Result (SAN14_MOD_CALL *is_spectator)(void* host_context, int32_t* out_active);
    /* UI thread. target_force_id must identify a live force from a fresh world
     * snapshot. The trusted adapter revalidates its ruler before takeover. */
    San14Result (SAN14_MOD_CALL *take_control)(void* host_context, uint32_t target_force_id);
    /* UI thread; append-only extension. generation is world.capture's
     * generation, expected_controlled_force is 0 when spectating, otherwise
     * the snapshot's player_force_id. A fresh click on that same force hands
     * control to AI; any other live force becomes the controlled force.
     * Stale generation/control state or an unstable native menu is rejected. */
    San14Result (SAN14_MOD_CALL *select_force)(void*,uint64_t generation,uint32_t expected_controlled_force,uint32_t selected_force);
} San14SpectatorApi;

#endif
