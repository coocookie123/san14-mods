#pragma once
#include "common.h"
#include "game_adapter_api.h"
namespace manager_core {
struct LoadedAdapter {HMODULE module=nullptr;San14GameAdapterApi api{};};
LoadedAdapter load_adapter(const fs::path& root);
San14Result start_adapter(LoadedAdapter&,HMODULE manager_module) noexcept;
}
