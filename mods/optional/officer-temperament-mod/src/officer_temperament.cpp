#include "san14_mod.h"

namespace {
const San14HostApi* host=nullptr;
const San14OfficerTemperamentApi* temperament=nullptr;
constexpr San14OfficerTemperamentOverride cao_cao={
    sizeof(San14OfficerTemperamentOverride),SAN14_TEMPERAMENT_ALL,{-10,20,0,10,0,10,0,20,0}};

San14Result SAN14_MOD_CALL load(San14ModContext*) {return SAN14_OK;}

San14Result SAN14_MOD_CALL enable(San14ModContext*) {
    return temperament->set_uniform_override(host->host_context,&cao_cao);
}
San14Result SAN14_MOD_CALL disable(San14ModContext*) {
    return temperament->set_uniform_override(host->host_context,nullptr);
}
void SAN14_MOD_CALL unload(San14ModContext*) {
    temperament=nullptr;
    host=nullptr;
}
}

extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(
    const San14HostApi* candidate,San14ModApi* out) {
    constexpr auto host_size=offsetof(San14HostApi,world)+sizeof(candidate->world);
    constexpr auto world_size=offsetof(San14WorldApi,officer_temperament)+sizeof(candidate->world->officer_temperament);
    if(!candidate||!out||candidate->struct_size<host_size||out->struct_size<sizeof(*out)||
       candidate->abi_version!=SAN14_HOST_ABI_V1||!candidate->world||candidate->world->struct_size<world_size)
        return SAN14_E_UNSUPPORTED;
    const auto* service=candidate->world->officer_temperament;
    if(!service||service->struct_size<sizeof(*service)||!service->set_uniform_override)return SAN14_E_UNSUPPORTED;
    if(host)return SAN14_E_BUSY;
    host=candidate;
    temperament=service;
    *out={sizeof(*out),SAN14_HOST_ABI_V1,load,enable,disable,unload};
    return SAN14_OK;
}
