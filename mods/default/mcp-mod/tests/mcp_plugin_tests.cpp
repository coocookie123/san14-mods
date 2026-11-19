#include "san14_mod.h"
#include "san14_automation.h"
#include "san14_idle.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace fixture {
struct Task {bool complete=false;};
struct State {
    San14HostApi host{};San14ModApi plugin{};San14ModContext context{};
    San14ClipboardApi clipboard{};
    struct Button{San14ControlHandle handle;San14ControlCallback callback;void* context;};
    std::map<std::string,Button> buttons;std::string copied;San14Result copy_result=SAN14_OK;
    San14AutomationApi automation{};San14McpApi mcp{};San14PagesApi pages{};
    San14SettingsPageOpen open=nullptr;void* open_context=nullptr;
    San14EventCallback idle=nullptr;void* idle_context=nullptr;
    San14McpState service_state=SAN14_MCP_STOPPED;
    uint32_t service_port=8765;
    San14Result start_result=SAN14_OK,stop_result=SAN14_OK,status_result=SAN14_OK;
    std::map<San14Handle,Task> tasks;std::map<San14Handle,std::string> labels;
    uint64_t sequence=10;San14PanelHandle panel=0;
    unsigned starts=0,stops=0,subscriptions=0,unsubscriptions=0,page_registrations=0,page_unregistrations=0,status_calls=0;
    static State& self(void* owner){return *static_cast<State*>(owner);}
    static std::string string(San14StringView value){return {value.data?value.data:"",value.length};}
    static San14Result SAN14_MOD_CALL submit(void*,San14StringView,San14StringView,San14AutomationTask*){return SAN14_E_UNSUPPORTED;}
    static San14Result SAN14_MOD_CALL poll(void* owner,San14AutomationTask handle,char*,uint32_t,uint32_t* required,int32_t* complete){
        auto& s=self(owner);auto found=s.tasks.find(handle);if(found==s.tasks.end())return SAN14_E_NOT_FOUND;
        *complete=found->second.complete?1:0;*required=*complete?3:0;return *complete?SAN14_E_BUFFER_TOO_SMALL:SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL release(void* owner,San14AutomationTask handle){return self(owner).tasks.erase(handle)?SAN14_OK:SAN14_E_NOT_FOUND;}
    static San14Result SAN14_MOD_CALL start(void* owner,const San14McpConfig* config,San14AutomationTask* output){
        auto& s=self(owner);++s.starts;assert(config&&config->struct_size==sizeof(*config)&&config->port==8765);
        if(s.start_result!=SAN14_OK)return s.start_result;
        *output=++s.sequence;s.tasks[*output]={false};s.service_state=SAN14_MCP_STARTING;return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL stop(void* owner,San14AutomationTask* output){
        auto& s=self(owner);++s.stops;if(s.stop_result!=SAN14_OK)return s.stop_result;
        *output=++s.sequence;s.tasks[*output]={false};s.service_state=SAN14_MCP_STOPPING;return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL status(void* owner,San14McpStatus* output){
        auto& s=self(owner);++s.status_calls;if(s.status_result!=SAN14_OK)return s.status_result;
        assert(output&&output->struct_size>=sizeof(*output));
        *output={sizeof(*output),s.service_state,s.service_port,{}};
        if(s.service_state==SAN14_MCP_FAILED){static constexpr char message[]="listener failed";std::memcpy(output->last_error,message,sizeof(message));}return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL register_page(void* owner,const San14SettingsPageSpec* spec){
        auto& s=self(owner);assert(!s.open);s.open=spec->open;s.open_context=spec->plugin_context;++s.page_registrations;return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL unregister_page(void* owner){auto& s=self(owner);s.open=nullptr;++s.page_unregistrations;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL subscribe(void* owner,uint32_t event,int32_t,San14EventCallback callback,void* context,San14Subscription* output){
        auto& s=self(owner);assert(event==SAN14_EVENT_UI_IDLE&&!s.idle);s.idle=callback;s.idle_context=context;*output=1;++s.subscriptions;return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL unsubscribe(void* owner,San14Subscription handle){auto& s=self(owner);assert(handle==1);s.idle=nullptr;++s.unsubscriptions;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL create(void* owner,const San14PanelSpec*,San14PanelHandle* output){auto& s=self(owner);assert(!s.panel);*output=s.panel=++s.sequence;return SAN14_OK;}
    static San14Result SAN14_MOD_CALL label(void* owner,San14PanelHandle panel,const San14ControlSpec* spec,San14ControlHandle* output){
        auto& s=self(owner);assert(panel==s.panel);*output=++s.sequence;s.labels[*output]=string(spec->text);return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL button(void* owner,San14PanelHandle panel,const San14ControlSpec* spec,San14ControlCallback callback,void* context,San14ControlHandle* output){
        auto result=label(owner,panel,spec,output);self(owner).buttons[string(spec->control_id)]={*output,callback,context};return result;
    }
    static San14Result SAN14_MOD_CALL copy(void* owner,San14StringView text){auto& s=self(owner);if(s.copy_result==SAN14_OK)s.copied=string(text);return s.copy_result;}
    void click(const std::string& id){auto b=buttons.at(id);b.callback(b.context,b.handle);}
    static San14Result SAN14_MOD_CALL set_text(void* owner,San14ControlHandle handle,San14StringView text){
        auto& s=self(owner);auto found=s.labels.find(handle);if(found==s.labels.end())return SAN14_E_NOT_FOUND;found->second=string(text);return SAN14_OK;
    }
    static San14Result SAN14_MOD_CALL show(void* owner,San14PanelHandle panel){assert(panel==self(owner).panel);return SAN14_OK;}
    static San14Result SAN14_MOD_CALL close(void* owner,San14PanelHandle panel){auto& s=self(owner);if(panel!=s.panel)return SAN14_E_NOT_FOUND;s.panel=0;s.labels.clear();s.buttons.clear();return SAN14_OK;}
    State(){
        automation={sizeof(automation),submit,poll,release};mcp={sizeof(mcp),start,stop,status};pages={sizeof(pages),register_page,unregister_page};
        host.struct_size=sizeof(host);host.abi_version=SAN14_HOST_ABI_V1;host.host_context=this;host.automation=&automation;host.mcp=&mcp;host.pages=&pages;
        clipboard={sizeof(clipboard),copy};host.clipboard=&clipboard;
        host.events={sizeof(host.events),subscribe,unsubscribe};
        host.ui={sizeof(host.ui),create,label,button,set_text,show,close,nullptr,nullptr};plugin.struct_size=sizeof(plugin);
    }
    void tick(uint64_t now){assert(idle);San14IdleInfo info{sizeof(info),now,1000};San14EventView event{sizeof(event),SAN14_EVENT_UI_IDLE,0,&info,sizeof(info),0};idle(idle_context,&event);}
    void complete_all(){for(auto& row:tasks)row.second.complete=true;}
    bool contains(const char* text)const{for(const auto& row:labels)if(row.second.find(text)!=std::string::npos)return true;return false;}
};
}

#include "../src/mcp_plugin.cpp"

int main(){
    fixture::State state;
    const auto size=state.host.struct_size;
    state.host.struct_size=offsetof(San14HostApi,mcp);
    assert(SAN14Mod_Initialize(&state.host,&state.plugin)==SAN14_E_UNSUPPORTED);state.host.struct_size=size;
    state.automation.poll=nullptr;assert(SAN14Mod_Initialize(&state.host,&state.plugin)==SAN14_E_UNSUPPORTED);state.automation.poll=fixture::State::poll;
    assert(SAN14Mod_Initialize(&state.host,&state.plugin)==SAN14_OK);
    assert(state.plugin.on_load(&state.context)==SAN14_OK&&state.page_registrations==1);
    // Disabled settings are informational and do not create worker/listener jobs.
    assert(state.open(state.open_context)==SAN14_OK&&state.starts==0&&state.status_calls==0);
    assert(state.contains("127.0.0.1:8765")&&state.contains(u8"已停用"));
    assert(state.contains(u8"先载入存档")&&state.contains(u8"首次设置"));
    state.click("mcp-copy-endpoint");assert(state.copied=="http://127.0.0.1:8765/mcp");
    state.click("mcp-copy-install");assert(state.copied.find("Streamable HTTP")!=std::string::npos&&state.copied.find(u8"测试连接")!=std::string::npos);
    const char* expected[]={u8"请告诉我三国志14 MCP都能帮我做什么",u8"请为我分析天下大势",u8"请直接接管我的势力帮我玩",u8"请帮我找到我的势力里所有有名声个性的武将"};
    for(int i=0;i<4;++i){state.click("mcp-example-"+std::to_string(i)+"-copy");assert(state.copied==expected[i]);}
    assert(state.starts==0&&state.contains(u8"已复制")); // copying never enables/commands the game
    const auto saved_copy=state.copied;state.copy_result=SAN14_E_BUSY;
    state.click("mcp-copy-install");assert(state.copied==saved_copy&&state.contains(u8"剪贴板正忙"));state.copy_result=SAN14_OK;
    state.host.struct_size=offsetof(San14HostApi,clipboard);state.click("mcp-copy-install");
    assert(state.copied==saved_copy&&state.contains(u8"不支持复制"));state.host.struct_size=size;
    // A rejected enable rolls back the idle subscription.
    state.start_result=SAN14_E_BUSY;assert(state.plugin.on_enable(&state.context)==SAN14_E_BUSY);
    assert(!state.idle&&state.subscriptions==state.unsubscriptions);state.start_result=SAN14_OK;
    assert(state.plugin.on_enable(&state.context)==SAN14_OK&&state.idle);
    state.tick(1000);assert(state.contains(u8"正在启动"));
    state.complete_all();state.service_state=SAN14_MCP_RUNNING;state.tick(1300);
    assert(state.tasks.empty()&&state.contains(u8"正在运行"));
    state.service_port=18765;state.tick(1550);
    assert(state.contains("127.0.0.1:18765"));
    state.click("mcp-copy-endpoint");assert(state.copied=="http://127.0.0.1:18765/mcp");
    state.click("mcp-copy-install");assert(state.copied.find("http://127.0.0.1:18765/mcp")!=std::string::npos);
    // Stop authorization is submitted once; running writes keep disable BUSY.
    assert(state.plugin.on_disable(&state.context)==SAN14_E_BUSY&&state.stops==1);
    assert(state.plugin.on_disable(&state.context)==SAN14_E_BUSY&&state.stops==1&&state.idle);
    state.complete_all();state.service_state=SAN14_MCP_STOPPED;state.tick(1600);
    assert(state.plugin.on_disable(&state.context)==SAN14_OK&&!state.idle&&!state.panel&&state.tasks.empty());
    assert(state.open&&state.page_unregistrations==0);
    // Failed asynchronous shutdown remains visible and is retryable.
    assert(state.plugin.on_enable(&state.context)==SAN14_OK);
    state.complete_all();state.service_state=SAN14_MCP_FAILED;state.tick(2000);
    assert(state.plugin.on_disable(&state.context)==SAN14_E_BUSY);
    state.complete_all();state.service_state=SAN14_MCP_FAILED;
    assert(state.plugin.on_disable(&state.context)==SAN14_E_PLUGIN_FAILED);
    const auto stops=state.stops;assert(state.plugin.on_disable(&state.context)==SAN14_E_BUSY&&state.stops==stops+1);
    state.complete_all();state.service_state=SAN14_MCP_STOPPED;
    assert(state.plugin.on_disable(&state.context)==SAN14_OK);
    // Page survives disable, and is removed only by unload.
    assert(state.open(state.open_context)==SAN14_OK&&state.contains(u8"已停用"));
    state.plugin.on_unload(&state.context);assert(!state.open&&!state.panel&&state.page_unregistrations==1);
    std::cout<<"MCP plugin lifecycle, async shutdown, status page and ABI negotiation passed\n";
}
