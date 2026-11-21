#pragma once
#include "json.h"
#include <cstdint>
#include <set>
namespace manifest_identity {
// The caller has already admitted the executable through GameProfile. Missing
// restrictions inherit that admission; declared game_builds are always enforced.
inline void admit_build(const json::Value& manifest,uint32_t actual_build){
    if(!manifest.has("game_builds"))return;
    const auto& builds=manifest.at("game_builds");
    if(builds.type!=json::Value::Array||builds.array.empty())throw std::runtime_error("game_builds must be a nonempty array");
    std::set<uint32_t> unique;bool match=false;
    for(const auto& value:builds.array){
        if(value.type!=json::Value::Number)throw std::runtime_error("Invalid game build");
        const auto build=value.integer();
        if(build<=0||build>UINT32_MAX||!unique.insert(static_cast<uint32_t>(build)).second)throw std::runtime_error("Invalid/duplicate game build");
        if(build==actual_build)match=true;
    }
    if(!match)throw std::runtime_error("Unsupported game build");
}
// Shared native admission helper, independent of UI and DLL loading.
// game_exe_sha256 is strictly enforced when game.memory.read is requested;
// pure semantic-API mods inherit Host's verified GameProfile/discovery admission.
inline bool admit(const json::Value& manifest,const std::set<std::string>& permissions,const std::string& actual_sha){
    const bool raw=permissions.count("game.memory.read")!=0;
    if(!manifest.has("game_exe_sha256")){
        if(raw)throw std::runtime_error("game.memory.read requires game_exe_sha256");return false;
    }
    const auto& hashes=manifest.at("game_exe_sha256");
    if(hashes.type!=json::Value::Array||hashes.array.empty())throw std::runtime_error("game_exe_sha256 must be a nonempty array");
    std::set<std::string> unique;bool match=false;
    for(const auto& value:hashes.array){auto hash=value.str();
        if(hash.size()!=64||hash.find_first_not_of("0123456789abcdef")!=std::string::npos||!unique.insert(hash).second)
            throw std::runtime_error("Invalid/duplicate executable SHA256");
        if(hash==actual_sha)match=true;
    }
    if(raw&&!match)throw std::runtime_error("Executable SHA256 incompatible");return raw;
}
}
