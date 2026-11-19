#include "san14_mod.h"
static San14Result SAN14_MOD_CALL ok(San14ModContext*){return SAN14_OK;}
extern "C" __declspec(dllexport) San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi*,San14ModApi* out){
    if(!out||out->struct_size<sizeof(*out))return SAN14_E_INVALID_ARGUMENT;
    out->struct_size=sizeof(*out);out->abi_version=SAN14_HOST_ABI_V1;out->on_load=ok;out->on_enable=ok;out->on_disable=ok;return SAN14_OK;
}
