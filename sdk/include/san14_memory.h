#ifndef SAN14_MOD_SDK_MEMORY_H
#define SAN14_MOD_SDK_MEMORY_H

#include "san14_types.h"

/* Experimental raw reads are opt-in: game.memory.read plus an exact executable
 * SHA-256 allowlist in mod.json. This is a compatibility contract, not a sandbox.
 * Addresses belong to this process only and must never be persisted across runs.
 * A successful copy does not establish object validity or snapshot consistency. */
#define SAN14_MEMORY_MAX_READ_BYTES UINT32_C(1048576)
#define SAN14_MODULE_GAME UINT32_C(1)

typedef struct San14ModuleInfo {
    uint32_t struct_size;
    uint32_t reserved;
    uint64_t base_address;
    uint64_t image_size;
    uint32_t game_build;
    uint32_t pointer_size;
    char module_name[64];
    /* Lowercase SHA-256 of the executable FILE admitted by the Host, not the
     * decrypted/patched runtime image. NUL terminated. */
    char exe_sha256[65];
    uint8_t reserved_v1_padding[7];
} San14ModuleInfo;

#define SAN14_MODULE_INFO_V1_SIZE ((uint32_t)(offsetof(San14ModuleInfo, reserved_v1_padding) + 7u))

typedef struct San14MemoryApi {
    uint32_t struct_size;
    /* Currently only SAN14_MODULE_GAME is supported. No loading or enumeration
     * of arbitrary DLLs. Set out_info->struct_size to your buffer capacity. */
    San14Result (SAN14_MOD_CALL *query_module)(
        void* host_context, uint32_t module_id, San14ModuleInfo* out_info);
    /* UI thread, enabled owner only. byte_count must be 1..MAX_READ_BYTES.
     * Copies into caller-owned writable storage; caller must keep it valid for
     * the entire call. Source failures leave that storage unchanged, returning
     * NOT_FOUND. Invalid ranges/sizes/buffers return INVALID_ARGUMENT.
     * No partial success; no game writes, protection changes or native calls.
     * Multiple reads are not an atomic snapshot, including within one callback. */
    San14Result (SAN14_MOD_CALL *read_memory)(
        void* host_context, uint64_t address, void* buffer, uint32_t byte_count);
} San14MemoryApi;

#endif
