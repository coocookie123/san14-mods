#ifndef SAN14_MOD_SDK_MOD_H
#define SAN14_MOD_SDK_MOD_H

#include "san14_host.h"

typedef struct San14ModContext {
    uint32_t struct_size;
    uint32_t reserved;
    San14StringView mod_id;
    San14StringView mod_root;
    void* plugin_context;
} San14ModContext;

typedef struct San14ModApi {
    uint32_t struct_size;
    uint32_t abi_version;
    San14Result (SAN14_MOD_CALL *on_load)(San14ModContext* context);
    San14Result (SAN14_MOD_CALL *on_enable)(San14ModContext* context);
    San14Result (SAN14_MOD_CALL *on_disable)(San14ModContext* context);
    void (SAN14_MOD_CALL *on_unload)(San14ModContext* context);
} San14ModApi;

#ifdef __cplusplus
extern "C" {
#endif

SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(
    const San14HostApi* host,
    San14ModApi* out_mod_api
);

#ifdef __cplusplus
}
#endif

#endif
