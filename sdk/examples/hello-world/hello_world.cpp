#include <cstring>

#include "san14_mod.h"

namespace {
const San14HostApi* g_host = nullptr;

San14StringView text(const char* value) {
    return {value, static_cast<uint32_t>(std::strlen(value))};
}

San14Result SAN14_MOD_CALL on_load(San14ModContext*) {
    if (!g_host || !g_host->log.write) return SAN14_E_INVALID_ARGUMENT;
    g_host->log.write(g_host->host_context, SAN14_LOG_INFO,
                      text("hello-world"), text("standard plugin loaded"));
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL on_enable(San14ModContext*) {
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL on_disable(San14ModContext*) {
    return SAN14_OK;
}

void SAN14_MOD_CALL on_unload(San14ModContext*) {
    g_host = nullptr;
}
}

extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(
    const San14HostApi* host,
    San14ModApi* out_mod_api
) {
    if (!host || !out_mod_api || host->abi_version != SAN14_HOST_ABI_V1 ||
        out_mod_api->struct_size < sizeof(San14ModApi)) {
        return SAN14_E_UNSUPPORTED;
    }
    g_host = host;
    San14ModApi api{};
    api.struct_size = sizeof(api);
    api.abi_version = SAN14_HOST_ABI_V1;
    api.on_load = on_load;
    api.on_enable = on_enable;
    api.on_disable = on_disable;
    api.on_unload = on_unload;
    *out_mod_api = api;
    return SAN14_OK;
}
