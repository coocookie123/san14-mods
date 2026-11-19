#include "adapter_loader.h"
#include "core_services.h"
#include "json.h"
#include <cstddef>
namespace manager_core {
static San14Result guarded_query(San14GameAdapterQuery fn,San14GameAdapterApi* output)noexcept{__try{return fn(SAN14_GAME_ADAPTER_ABI_V1,output);}__except(EXCEPTION_EXECUTE_HANDLER){return SAN14_E_PLUGIN_FAILED;}}
LoadedAdapter load_adapter(const fs::path& root){
    const auto base=fs::weakly_canonical(root/L"SAN14Base"/L"GameAdapter");
    const auto manifest_path=base/L"adapter.json";
    auto bytes=read_file(manifest_path,64*1024);auto manifest=json::parse(std::string(bytes.begin(),bytes.end()));
    if(manifest.at("schema_version").integer()!=1||manifest.at("abi_version").integer()!=SAN14_GAME_ADAPTER_ABI_V1||manifest.at("sdk_abi_version").integer()!=SAN14_HOST_ABI_V1)throw std::runtime_error("GameAdapter manifest ABI incompatible");
    if(manifest.at("binary").str()!="GameAdapter.dll")throw std::runtime_error("GameAdapter binary name refused");
    const auto path=base/L"GameAdapter.dll";
    if(!inside(base,path))throw std::runtime_error("GameAdapter path escaped");
    const auto expected=manifest.at("sha256").str();if(expected.size()!=64||expected.find_first_not_of("0123456789abcdef")!=std::string::npos||hash_file(path)!=expected)throw std::runtime_error("GameAdapter integrity mismatch");
    LoadedAdapter out;out.module=LoadLibraryExW(path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);if(!out.module)throw std::runtime_error("GameAdapter DLL could not be loaded");
    try{
        auto query=reinterpret_cast<San14GameAdapterQuery>(GetProcAddress(out.module,"SAN14GameAdapter_Query"));
        out.api.struct_size=sizeof(out.api);
        if(!query||guarded_query(query,&out.api)!=SAN14_OK||out.api.struct_size<sizeof(out.api)||out.api.abi_version!=SAN14_GAME_ADAPTER_ABI_V1||out.api.sdk_abi_version!=SAN14_HOST_ABI_V1||out.api.required_core_size>api()->struct_size||!out.api.adapter_id||!out.api.adapter_version||!out.api.start)throw std::runtime_error("GameAdapter binary ABI incompatible");
        if(manifest.at("adapter_id").str()!=out.api.adapter_id||manifest.at("adapter_version").str()!=out.api.adapter_version)throw std::runtime_error("GameAdapter identity mismatch");
        return out;
    }catch(...){FreeLibrary(out.module);throw;}
}
San14Result start_adapter(LoadedAdapter& adapter,HMODULE module)noexcept{
    if(!adapter.module||!adapter.api.start)return SAN14_E_INVALID_ARGUMENT;
    San14GameAdapterStart start{sizeof(start),SAN14_GAME_ADAPTER_ABI_V1,module,adapter.module,api()};
    __try{return adapter.api.start(&start);}__except(EXCEPTION_EXECUTE_HANDLER){return SAN14_E_PLUGIN_FAILED;}
}
}
