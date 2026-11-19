#include "adapter_loader.h"
#include <mutex>
namespace {
static manager_core::LoadedAdapter adapter;
static void record(const std::string& message)noexcept{try{auto path=local_data();fs::create_directories(path);std::ofstream out(path/L"runtime.log",std::ios::binary|std::ios::app);out<<"source=manager-core [adapter] "<<message<<"\n";}catch(...){}}
static DWORD run(void* module)noexcept{
    try{adapter=manager_core::load_adapter(module_path(static_cast<HMODULE>(module)).parent_path());record(std::string("loaded ")+adapter.api.adapter_id+" version="+adapter.api.adapter_version);auto result=manager_core::start_adapter(adapter,static_cast<HMODULE>(module));if(result!=SAN14_OK)record("start failed result="+std::to_string(result));}
    catch(const std::exception& e){record(e.what());MessageBoxW(nullptr,L"游戏适配包缺失、不完整或版本不兼容，模组管理器未启用。\n请安装匹配的 GameAdapter DLL 包。\n详细原因见管理器运行日志。",L"模组管理器未启用",MB_OK|MB_ICONWARNING);}
    catch(...){record("unexpected adapter bootstrap failure");}
    // Keep the module pinned: native hooks/callbacks may remain until process exit.
    return 0;
}
}
DWORD WINAPI StartSan14Host(void* module){return run(module);}
