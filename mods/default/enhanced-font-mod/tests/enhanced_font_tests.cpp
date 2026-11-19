#include "../src/enhanced_font.cpp"
#include <cstdio>
#include <cstdlib>
#include <string>

#define CHECK(expr) do { if (!(expr)) { std::fprintf(stderr,"failed at line %d: %s\n",__LINE__,#expr); std::exit(1); } } while (0)

namespace test {
struct State {
    uint32_t scale=1,mask=SAN14_TEXT_RENDERING_SCALE_NATIVE|SAN14_TEXT_RENDERING_SCALE_2X;
    uint32_t caps_calls=0,set_calls=0;
    San14Result query_result=SAN14_OK,set_result=SAN14_OK;
    San14Result settings_result=SAN14_OK;
    int32_t enabled=1;
    uint32_t registered=0;
};
San14Result SAN14_MOD_CALL register_bool(void* ctx,const San14BoolSetting* spec){
    auto& state=*static_cast<State*>(ctx);++state.registered;
    CHECK(spec&&spec->default_value==1&&spec->flags==SAN14_BOOL_SETTING_RESTART_REQUIRED);
    CHECK(std::string(spec->key.data,spec->key.length)=="text.high_resolution");
    CHECK(std::string(spec->label.data,spec->label.length)==u8"增强文本分辨率");
    return state.settings_result;
}
San14Result SAN14_MOD_CALL get_bool(void* ctx,San14StringView,int32_t* out){
    auto& state=*static_cast<State*>(ctx);*out=state.enabled;return state.settings_result;
}
San14Result SAN14_MOD_CALL query(void* ctx,San14TextRenderingCaps* out) {
    auto& state=*static_cast<State*>(ctx);++state.caps_calls;
    CHECK(out&&out->struct_size==sizeof(*out));
    if(state.query_result!=SAN14_OK)return state.query_result;
    *out={sizeof(*out),state.mask,SAN14_TEXT_RENDERING_RESTART_RECOMMENDED,0};return SAN14_OK;
}
San14Result SAN14_MOD_CALL set(void* ctx,uint32_t scale) {
    auto& state=*static_cast<State*>(ctx);++state.set_calls;
    CHECK(scale==1||scale==2);
    if(state.set_result!=SAN14_OK)return state.set_result;
    state.scale=scale;return SAN14_OK;
}
San14Result SAN14_MOD_CALL get(void* ctx,uint32_t* out) {*out=static_cast<State*>(ctx)->scale;return SAN14_OK;}
}

int main() {
    test::State state;
    San14TextRenderingApi service{sizeof(service),test::query,test::set,test::get};
    San14HostApi host{};host.struct_size=sizeof(host);host.abi_version=SAN14_HOST_ABI_V1;host.host_context=&state;host.text_rendering=&service;
    host.settings={sizeof(San14SettingsApi),test::register_bool,test::get_bool};
    San14ModApi api{};api.struct_size=sizeof(api);
    CHECK(SAN14Mod_Initialize(nullptr,&api)==SAN14_E_UNSUPPORTED);
    CHECK(SAN14Mod_Initialize(&host,nullptr)==SAN14_E_UNSUPPORTED);
    host.abi_version=0;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);host.abi_version=SAN14_HOST_ABI_V1;
    api.struct_size=sizeof(api)-1;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);api.struct_size=sizeof(api);
    host.struct_size=offsetof(San14HostApi,text_rendering);CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);host.struct_size=sizeof(host);
    host.text_rendering=nullptr;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);host.text_rendering=&service;
    service.struct_size=offsetof(San14TextRenderingApi,get_cache_scale);CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);service.struct_size=sizeof(service);
    service.get_cache_scale=nullptr;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);service.get_cache_scale=test::get;
    service.set_cache_scale=nullptr;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);service.set_cache_scale=test::set;
    service.query_caps=nullptr;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);service.query_caps=test::query;
    host.settings.register_bool=nullptr;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);host.settings.register_bool=test::register_bool;
    host.settings.get_bool=nullptr;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);host.settings.get_bool=test::get_bool;
    host.settings.struct_size=0;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_UNSUPPORTED);host.settings.struct_size=sizeof(San14SettingsApi);
    CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_OK);
    CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_E_BUSY);
    state.settings_result=SAN14_E_INVALID_ARGUMENT;CHECK(api.on_load(nullptr)==SAN14_E_INVALID_ARGUMENT);
    state.settings_result=SAN14_OK;CHECK(api.on_load(nullptr)==SAN14_OK&&state.set_calls==0&&state.registered==2);
    state.settings_result=SAN14_E_PERMISSION_DENIED;CHECK(api.on_enable(nullptr)==SAN14_E_PERMISSION_DENIED&&state.set_calls==0);state.settings_result=SAN14_OK;
    state.query_result=SAN14_E_PERMISSION_DENIED;
    CHECK(api.on_enable(nullptr)==SAN14_E_PERMISSION_DENIED&&state.scale==1&&state.set_calls==0);
    CHECK(api.on_disable(nullptr)==SAN14_OK&&state.set_calls==0);
    state.query_result=SAN14_OK;state.mask=SAN14_TEXT_RENDERING_SCALE_NATIVE;
    CHECK(api.on_enable(nullptr)==SAN14_E_UNSUPPORTED&&state.set_calls==0);
    state.mask|=SAN14_TEXT_RENDERING_SCALE_2X;state.set_result=SAN14_E_BUSY;
    CHECK(api.on_enable(nullptr)==SAN14_E_BUSY&&state.scale==1);
    const auto failed_calls=state.set_calls;
    CHECK(api.on_disable(nullptr)==SAN14_OK&&state.set_calls==failed_calls);
    state.set_result=SAN14_OK;
    CHECK(api.on_enable(nullptr)==SAN14_OK&&state.scale==2);
    state.enabled=0; // Pending setting does not change an already running lease.
    const auto pending_calls=state.set_calls;CHECK(api.on_enable(nullptr)==SAN14_OK&&state.scale==2&&state.set_calls==pending_calls);state.enabled=1;
    const auto enabled_calls=state.set_calls;
    CHECK(api.on_enable(nullptr)==SAN14_OK&&state.set_calls==enabled_calls);
    state.set_result=SAN14_E_PLUGIN_FAILED;
    CHECK(api.on_disable(nullptr)==SAN14_E_PLUGIN_FAILED&&state.scale==2);
    state.set_result=SAN14_OK;
    CHECK(api.on_disable(nullptr)==SAN14_OK&&state.scale==1);
    const auto disabled_calls=state.set_calls;
    CHECK(api.on_disable(nullptr)==SAN14_OK&&state.set_calls==disabled_calls);
    CHECK(api.on_enable(nullptr)==SAN14_OK&&state.scale==2);
    api.on_unload(nullptr);CHECK(state.scale==1);
    state.enabled=0;CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_OK);
    const auto off_caps=state.caps_calls,off_calls=state.set_calls;
    CHECK(api.on_enable(nullptr)==SAN14_OK&&state.scale==1&&state.caps_calls==off_caps&&state.set_calls==off_calls);
    CHECK(api.on_disable(nullptr)==SAN14_OK&&state.set_calls==off_calls);api.on_unload(nullptr);state.enabled=1;
    // Future tail additions remain compatible; only known prefixes are read.
    host.struct_size=sizeof(host)+16;service.struct_size=sizeof(service)+16;
    CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_OK);
    CHECK(api.on_enable(nullptr)==SAN14_OK);api.on_unload(nullptr);CHECK(state.scale==1);
    CHECK(SAN14Mod_Initialize(&host,&api)==SAN14_OK);api.on_unload(nullptr);
    std::puts("Enhanced-font plugin compatibility and lifecycle tests passed.");
}
