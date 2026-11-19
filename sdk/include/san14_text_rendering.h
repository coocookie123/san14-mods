#ifndef SAN14_MOD_SDK_TEXT_RENDERING_H
#define SAN14_MOD_SDK_TEXT_RENDERING_H

#include "san14_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* supported_scales uses bit (1u << scale). Only request advertised scales. */
#define SAN14_TEXT_RENDERING_SCALE_NATIVE (1u << 1)
#define SAN14_TEXT_RENDERING_SCALE_2X (1u << 2)
#define SAN14_TEXT_RENDERING_RESTART_RECOMMENDED 1u

typedef struct San14TextRenderingCaps {
    uint32_t struct_size;
    uint32_t supported_scales;
    uint32_t flags;
    uint32_t reserved;
} San14TextRenderingCaps;

/* Optional service. All calls require an enabled owner, "game.rules", and the
 * UI thread. Check Host/service size, pointer and function pointers before use.
 * Host owns the native rendering hooks. */
typedef struct San14TextRenderingApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *query_caps)(
        void* host_context, San14TextRenderingCaps* out_caps);
    /* Scale 2 requests double-resolution caches without resizing the UI.
     * One owner holds the request; a competing owner receives BUSY.
     * Scale 1 releases the caller's lease. Cleanup also releases the lease.
     * Release stops enlarged cache requests; existing textures keep their
     * rendering correction until naturally rebuilt. Restart for a uniform mode.
     * This service does not imply font-family replacement. */
    San14Result (SAN14_MOD_CALL *set_cache_scale)(
        void* host_context, uint32_t scale);
    /* This owner's request (1 or 2), not every extant texture's scale. */
    San14Result (SAN14_MOD_CALL *get_cache_scale)(
        void* host_context, uint32_t* out_scale);
} San14TextRenderingApi;

#ifdef __cplusplus
}
#endif

#endif
