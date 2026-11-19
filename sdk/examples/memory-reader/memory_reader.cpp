#include <cstddef>
#include <cstring>
#include "san14_mod.h"

namespace {
const San14HostApi* host=nullptr;
San14StringView text(const char* value){return {value,static_cast<uint32_t>(std::strlen(value))};}
San14Result SAN14_MOD_CALL load(San14ModContext*){return SAN14_OK;}
San14Result SAN14_MOD_CALL enable(San14ModContext*){
    // Raw reads are denied until enable; never store game addresses on disk.
    San14ModuleInfo game{};game.struct_size=sizeof(game);
    auto result=host->memory->query_module(host->host_context,SAN14_MODULE_GAME,&game);
    if(result!=SAN14_OK)return result;
    if(game.game_build!=25953386||game.pointer_size!=8||game.image_size<2||
       std::strcmp(game.exe_sha256,"2e49348aa1b9be5cfca651e71c8d77b2e4bd2d11387edc0b0236f4352458a239"))
        return SAN14_E_UNSUPPORTED;
    unsigned char header[2]{};
    result=host->memory->read_memory(host->host_context,game.base_address,header,sizeof(header));
    if(result!=SAN14_OK)return result;
    if(header[0]!='M'||header[1]!='Z')return SAN14_E_UNSUPPORTED;
    host->log.write(host->host_context,SAN14_LOG_INFO,text("memory-reader"),text("Admitted game module MZ header copied through Host"));
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL disable(San14ModContext*){return SAN14_OK;}
void SAN14_MOD_CALL unload(San14ModContext*){host=nullptr;}
}
extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi* h,San14ModApi* out){
    if(!h||!out||h->struct_size<offsetof(San14HostApi,memory)+sizeof(h->memory)||
       h->abi_version!=SAN14_HOST_ABI_V1||out->struct_size<sizeof(*out)||
       !h->memory||h->memory->struct_size<sizeof(San14MemoryApi)||
       !h->memory->query_module||!h->memory->read_memory||!h->log.write)return SAN14_E_UNSUPPORTED;
    host=h;*out={sizeof(*out),SAN14_HOST_ABI_V1,load,enable,disable,unload};return SAN14_OK;
}
