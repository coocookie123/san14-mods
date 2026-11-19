#include "game_adapter_api.h"
#include <cstddef>
#ifndef FIXTURE_BAD_ABI
#define FIXTURE_BAD_ABI 0
#endif
static San14Result SAN14_ADAPTER_CALL start(const San14GameAdapterStart* s){
    if(!s||s->struct_size<sizeof(*s)||!s->core||s->core->abi_version!=SAN14_MANAGER_CORE_ABI_V1||s->core->struct_size<sizeof(*s->core))return SAN14_E_INVALID_ARGUMENT;
    int32_t match=0;if(s->core->version_matches("0.2.0",">=0.2.0 <0.3.0",&match)!=SAN14_OK||!match||!s->core->valid_id("fixture-mod"))return SAN14_E_PLUGIN_FAILED;
    return SAN14_OK;
}
extern "C" __declspec(dllexport) San14Result SAN14_ADAPTER_CALL SAN14GameAdapter_Query(uint32_t version,San14GameAdapterApi* out){
    if(!out||out->struct_size<sizeof(*out)||version!=SAN14_GAME_ADAPTER_ABI_V1)return SAN14_E_UNSUPPORTED;
    *out={sizeof(*out),FIXTURE_BAD_ABI?99u:SAN14_GAME_ADAPTER_ABI_V1,SAN14_HOST_ABI_V1,sizeof(San14ManagerCoreApi),"fixture-adapter","0.1.0",start};return SAN14_OK;
}
