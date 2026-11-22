#include "adapter_loader.h"
#include "core_services.h"
#include "json.h"
#include <cassert>
#include <iostream>
static San14Result SAN14_MOD_CALL crashed(San14ModContext*){RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return SAN14_OK;}
int main(){
    auto api=manager_core::api();int32_t result=0;
    assert(api->version_matches("0.2.0",">=0.2.0 <0.3.0",&result)==SAN14_OK&&result);
    assert(api->version_matches("0.2.0",">=0.3.0",&result)==SAN14_OK&&!result);
    assert(api->version_matches("broken","*",&result)==SAN14_E_INVALID_ARGUMENT);
    assert(api->valid_id("fixture-mod")&&!api->valid_id("../escape")&&!api->valid_id("Bad-ID"));
    assert(api->valid_url("https://example.com/a")&&!api->valid_url("http://example.com"));
    auto root=fs::current_path()/(L"core-fixture-"+std::to_wstring(GetCurrentProcessId()));fs::create_directory(root);
    const char data[]="persisted";uint32_t required=0;char output[32]{};
    assert(api->global_write(root.c_str(),"fixture-mod","setting",data,sizeof(data))==SAN14_OK);
    assert(api->global_read(root.c_str(),"fixture-mod","setting",nullptr,0,&required)==SAN14_E_BUFFER_TOO_SMALL&&required==sizeof(data));
    assert(api->global_read(root.c_str(),"fixture-mod","setting",output,sizeof(output),&required)==SAN14_OK&&std::string(output)==data);
    assert(api->global_write(root.c_str(),"fixture-mod","../escape",data,sizeof(data))==SAN14_E_INVALID_ARGUMENT);
    San14ModContext context{};assert(api->lifecycle(&context,crashed)==SAN14_E_PLUGIN_FAILED);
    fs::copy_file("fixture-plugin.dll",root/L"fixture.dll");auto hash=hash_file(root/L"fixture.dll");San14HostApi host{};host.struct_size=sizeof(host);host.abi_version=SAN14_HOST_ABI_V1;San14ModApi plugin{};void* module=nullptr;
    assert(api->load_plugin((root/L"fixture.dll").c_str(),"bad-hash",&host,&plugin,&module)!=SAN14_OK&&!module);
    assert(api->load_plugin((root/L"fixture.dll").c_str(),hash.c_str(),&host,&plugin,&module)==SAN14_OK&&module);
    assert(api->lifecycle(&context,plugin.on_load)==SAN14_OK);api->release_plugin(module);
    const std::string manifest=R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_builds":[1],"dependencies":[],"conflicts":[],"permissions":["storage.saved-edit"],"disable_mode":"hot"})";
    write_atomic(root/L"mod.json",manifest.data(),manifest.size());San14CoreAdmission identity{sizeof(identity),1,"0.2.0","unused"};char error[512]{};
    assert(api->admit_mod(root.c_str(),&identity,nullptr,0,&required,error,sizeof(error))==SAN14_E_BUFFER_TOO_SMALL);
    std::vector<char> normalized(required);assert(api->admit_mod(root.c_str(),&identity,normalized.data(),required,&required,error,sizeof(error))==SAN14_OK);
    assert(json::parse(normalized.data()).at("_sha256").str()==hash);
    auto credits=[&](const std::string& value){auto text=manifest;text.insert(1,"\"coauthors\":"+value+",");write_atomic(root/L"mod.json",text.data(),text.size());return api->admit_mod(root.c_str(),&identity,nullptr,0,&required,error,sizeof(error));};
    assert(credits(u8R"([{"name":"黑色曲奇","url":"https://steamcommunity.com/profiles/76561198090135353"}])")==SAN14_E_BUFFER_TOO_SMALL);
    normalized.resize(required);assert(api->admit_mod(root.c_str(),&identity,normalized.data(),required,&required,error,sizeof(error))==SAN14_OK);
    assert(json::parse(normalized.data()).at("coauthors").array[0].at("url").str()=="https://steamcommunity.com/profiles/76561198090135353");
    for(const auto& value:{"null","{}","[1]","[{\"name\":\"\"}]","[{\"name\":\"a\",\"url\":\"javascript:alert(1)\"}]","[{\"name\":\"a\"},{\"name\":\"a\"}]"})assert(credits(value)==SAN14_E_INVALID_ARGUMENT);
    const std::string authors_manifest=R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","authors":[{"name":"AuthorA","url":"https://example.com/a"},{"name":"AuthorB","url":"https://example.com/b"}],"contributors":[{"name":"Helper","url":"https://example.com/h"}],"entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_builds":[1],"dependencies":[],"conflicts":[],"permissions":["storage.saved-edit"],"disable_mode":"hot"})";
    write_atomic(root/L"mod.json",authors_manifest.data(),authors_manifest.size());
    assert(api->admit_mod(root.c_str(),&identity,nullptr,0,&required,error,sizeof(error))==SAN14_E_BUFFER_TOO_SMALL);
    normalized.assign(required,0);assert(api->admit_mod(root.c_str(),&identity,normalized.data(),required,&required,error,sizeof(error))==SAN14_OK);
    assert(json::parse(normalized.data()).at("authors").array.size()==2);
    for(const std::string& bad:{std::string(R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","authors":[],"entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_builds":[1],"dependencies":[],"conflicts":[],"permissions":["storage.saved-edit"],"disable_mode":"hot"})"),std::string(R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","authors":[{"name":"Dup"},{"name":"Dup"}],"entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_builds":[1],"dependencies":[],"conflicts":[],"permissions":["storage.saved-edit"],"disable_mode":"hot"})"),std::string(R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","authors":[{"name":"BadUrl","url":"http://example.com"}],"entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_builds":[1],"dependencies":[],"conflicts":[],"permissions":["storage.saved-edit"],"disable_mode":"hot"})")}){
        write_atomic(root/L"mod.json",bad.data(),bad.size());
        assert(api->admit_mod(root.c_str(),&identity,nullptr,0,&required,error,sizeof(error))!=SAN14_OK);
    }
    write_atomic(root/L"mod.json",manifest.data(),manifest.size());
    identity.game_build=2;assert(api->admit_mod(root.c_str(),&identity,nullptr,0,&required,error,sizeof(error))!=SAN14_OK);

    // Pure semantic-API mod without game_builds / game_exe_sha256 is admitted cleanly
    // even on dynamically admitted discovery builds (game_build == 0).
    const std::string sha_a(64,'a'),sha_b(64,'b');
    const std::string semantic_manifest=R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","dependencies":[],"conflicts":[],"permissions":["game.rules","settings"],"disable_mode":"hot"})";
    write_atomic(root/L"mod.json",semantic_manifest.data(),semantic_manifest.size());
    San14CoreAdmission discovery_id{sizeof(discovery_id),0,"0.2.0",sha_b.c_str()};
    assert(api->admit_mod(root.c_str(),&discovery_id,nullptr,0,&required,error,sizeof(error))==SAN14_E_BUFFER_TOO_SMALL);
    normalized.assign(required,0);
    assert(api->admit_mod(root.c_str(),&discovery_id,normalized.data(),required,&required,error,sizeof(error))==SAN14_OK);
    assert(!json::parse(normalized.data()).at("_memory_admitted").flag());

    // Pure semantic-API mod carrying legacy game_exe_sha256 is not blocked by SHA mismatch
    // (still validates array format and never grants raw memory access).
    const std::string semantic_with_sha=std::string(R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_exe_sha256":[")")+sha_a+R"("],"dependencies":[],"conflicts":[],"permissions":["game.rules"],"disable_mode":"hot"})";
    write_atomic(root/L"mod.json",semantic_with_sha.data(),semantic_with_sha.size());
    assert(api->admit_mod(root.c_str(),&discovery_id,nullptr,0,&required,error,sizeof(error))==SAN14_E_BUFFER_TOO_SMALL);
    normalized.assign(required,0);
    assert(api->admit_mod(root.c_str(),&discovery_id,normalized.data(),required,&required,error,sizeof(error))==SAN14_OK);
    assert(!json::parse(normalized.data()).at("_memory_admitted").flag());

    const std::string semantic_bad_sha=R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_exe_sha256":["not-a-sha"],"dependencies":[],"conflicts":[],"permissions":["game.rules"],"disable_mode":"hot"})";
    write_atomic(root/L"mod.json",semantic_bad_sha.data(),semantic_bad_sha.size());
    assert(api->admit_mod(root.c_str(),&discovery_id,nullptr,0,&required,error,sizeof(error))!=SAN14_OK);

    // Raw-memory mod (game.memory.read) strictly requires matching game_exe_sha256.
    const std::string raw_missing_sha=R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","dependencies":[],"conflicts":[],"permissions":["game.memory.read"],"disable_mode":"hot"})";
    write_atomic(root/L"mod.json",raw_missing_sha.data(),raw_missing_sha.size());
    assert(api->admit_mod(root.c_str(),&discovery_id,nullptr,0,&required,error,sizeof(error))!=SAN14_OK);

    const std::string raw_with_sha=std::string(R"({"schema_version":1,"id":"fixture-mod","name":"Fixture","version":"0.1.0","entry":{"type":"native","path":"fixture.dll","abi":1},"host_version":">=0.2.0 <0.3.0","game_exe_sha256":[")")+sha_a+R"("],"dependencies":[],"conflicts":[],"permissions":["game.memory.read"],"disable_mode":"hot"})";
    write_atomic(root/L"mod.json",raw_with_sha.data(),raw_with_sha.size());
    assert(api->admit_mod(root.c_str(),&discovery_id,nullptr,0,&required,error,sizeof(error))!=SAN14_OK);
    discovery_id.game_exe_sha256=sha_a.c_str();
    assert(api->admit_mod(root.c_str(),&discovery_id,nullptr,0,&required,error,sizeof(error))==SAN14_E_BUFFER_TOO_SMALL);
    normalized.assign(required,0);
    assert(api->admit_mod(root.c_str(),&discovery_id,normalized.data(),required,&required,error,sizeof(error))==SAN14_OK);
    assert(json::parse(normalized.data()).at("_memory_admitted").flag());
    auto adapter=root/L"SAN14Base"/L"GameAdapter";fs::create_directories(adapter);
    auto put=[&](const fs::path& dll,const std::string& expected_id="fixture-adapter"){
        fs::copy_file(dll,adapter/L"GameAdapter.dll",fs::copy_options::overwrite_existing);
        auto json=std::string("{\"schema_version\":1,\"abi_version\":1,\"sdk_abi_version\":65536,\"adapter_id\":\"")+expected_id+"\",\"adapter_version\":\"0.1.0\",\"binary\":\"GameAdapter.dll\",\"sha256\":\""+hash_file(adapter/L"GameAdapter.dll")+"\"}";
        write_atomic(adapter/L"adapter.json",json.data(),json.size());
    };
    auto refused=[&]{try{auto loaded=manager_core::load_adapter(root);FreeLibrary(loaded.module);return false;}catch(...){return true;}};
    assert(refused());put("fixture-bad.dll");assert(refused());put("fixture-adapter.dll","wrong-id");assert(refused());
    put("fixture-adapter.dll");{std::ofstream f(adapter/L"GameAdapter.dll",std::ios::binary|std::ios::app);f<<'x';}assert(refused());
    put("fixture-adapter.dll");auto loaded=manager_core::load_adapter(root);assert(manager_core::start_adapter(loaded,GetModuleHandleW(nullptr))==SAN14_OK);FreeLibrary(loaded.module);
    // This unique fixture directory is created and owned by this test.
    fs::remove_all(root);std::cout<<"Core admission, plugin lifecycle, persistence and adapter ABI/integrity integration passed\n";
}
