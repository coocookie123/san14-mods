#include <cstddef>
#include <cstring>
#include <string>
#include "san14_mod.h"
#include "san14_automation.h"
#include "san14_idle.h"
#include "mcp_help.h"

namespace {
const San14HostApi* host=nullptr;
bool active=false,stopping=false,stop_requested=false,page_registered=false;
San14AutomationTask start_task=0,stop_task=0;
San14Subscription idle_subscription=0;
San14PanelHandle panel=0;
San14ControlHandle status_label=0,error_label=0,copy_label=0,endpoint_label=0;
int copy_actions[]={0,1,2,3,4,5};
uint64_t last_refresh=0;
San14McpStatus cached{sizeof(San14McpStatus),SAN14_MCP_STOPPED,SAN14_MCP_DEFAULT_PORT,{}};

San14StringView view(const char* text){return {text,static_cast<uint32_t>(std::strlen(text))};}
void log(San14LogLevel level,const char* text){if(host&&host->log.write)host->log.write(host->host_context,level,view("mcp"),view(text));}
bool available(){
    return host&&host->struct_size>=offsetof(San14HostApi,mcp)+sizeof(host->mcp)&&
        host->automation&&host->automation->struct_size>=sizeof(San14AutomationApi)&&host->automation->submit&&host->automation->poll&&host->automation->release&&
        host->mcp&&host->mcp->struct_size>=sizeof(San14McpApi)&&host->mcp->start&&host->mcp->stop&&host->mcp->status;
}
void forget_panel(){panel=0;status_label=error_label=copy_label=endpoint_label=0;}
San14Result close_panel(){
    if(!panel)return SAN14_OK;
    const auto result=host->ui.close_panel(host->host_context,panel);
    if(result==SAN14_OK||result==SAN14_E_NOT_FOUND){forget_panel();return SAN14_OK;}return result;
}
void SAN14_MOD_CALL back(void*,San14ControlHandle){close_panel();}
const char* state_text(){
    if(stopping)return u8"正在停止，等待已提交的操作完成";
    if(!active)return u8"已停用";
    switch(cached.state){
        case SAN14_MCP_STOPPED:return u8"已停止";
        case SAN14_MCP_STARTING:return u8"正在启动";
        case SAN14_MCP_RUNNING:return u8"正在运行";
        case SAN14_MCP_STOPPING:return u8"正在停止";
        case SAN14_MCP_FAILED:return u8"启动或运行失败";
        default:return u8"状态不可用";
    }
}
void paint(){
    if(!panel||!status_label)return;
    const std::string state=std::string(u8"服务状态：")+state_text();
    const auto result=host->ui.set_text(host->host_context,status_label,view(state.c_str()));
    if(result==SAN14_E_NOT_FOUND){forget_panel();return;}
    if(error_label)host->ui.set_text(host->host_context,error_label,view(cached.last_error));
    if(endpoint_label){const auto endpoint=mcp_help::endpoint_for(cached.port);host->ui.set_text(host->host_context,endpoint_label,view(endpoint.c_str()));}
}
bool release_task(San14AutomationTask& task){
    if(!task)return true;
    const auto result=host->automation->release(host->host_context,task);
    if(result==SAN14_OK||result==SAN14_E_NOT_FOUND){task=0;return true;}return false;
}
bool poll_task(San14AutomationTask& task){
    if(!task)return true;
    uint32_t required=0;int32_t complete=0;
    const auto result=host->automation->poll(host->host_context,task,nullptr,0,&required,&complete);
    if(result==SAN14_E_NOT_FOUND){task=0;return true;}
    if((result==SAN14_OK||result==SAN14_E_BUFFER_TOO_SMALL)&&complete)return release_task(task);
    return false;
}
San14Result refresh(){
    if(!active&&!stopping)return SAN14_OK;
    poll_task(start_task);poll_task(stop_task);
    San14McpStatus next{};next.struct_size=sizeof(next);
    const auto result=host->mcp->status(host->host_context,&next);
    if(result==SAN14_OK){
        next.last_error[sizeof(next.last_error)-1]=0;cached=next;
    }else{
        static constexpr char message[]=u8"暂时无法读取服务状态。";
        std::memcpy(cached.last_error,message,sizeof(message));
    }
    paint();return result;
}
void SAN14_MOD_CALL idle(void*,const San14EventView* event){
    if(!event||event->event_id!=SAN14_EVENT_UI_IDLE||(!active&&!stopping))return;
    uint64_t now=0;
    if(event->payload&&event->payload_size>=sizeof(San14IdleInfo)){
        const auto* info=static_cast<const San14IdleInfo*>(event->payload);
        if(info->struct_size>=sizeof(*info))now=info->monotonic_ms;
    }
    if(now&&last_refresh&&now>=last_refresh&&now-last_refresh<250)return;
    last_refresh=now;refresh();
}
bool can_copy(){
    return host&&host->struct_size>=offsetof(San14HostApi,clipboard)+sizeof(host->clipboard)&&host->clipboard&&
        host->clipboard->struct_size>=sizeof(San14ClipboardApi)&&host->clipboard->copy_text;
}
void SAN14_MOD_CALL copy_text(void* context,San14ControlHandle){
    if(!panel||!context)return;
    const int action=*static_cast<int*>(context);
    const auto content=action==0?mcp_help::endpoint_for(cached.port):action==1?mcp_help::install_for(cached.port):std::string(mcp_help::prompts[action-2]);
    const char* text=content.c_str();
    const auto result=can_copy()?host->clipboard->copy_text(host->host_context,view(text)):SAN14_E_UNSUPPORTED;
    const char* message=result==SAN14_OK?u8"已复制，请粘贴给你的 AI 智能体。":result==SAN14_E_BUSY?u8"剪贴板正忙，请再点一次复制。":result==SAN14_E_UNSUPPORTED?u8"当前管理器不支持复制，请更新管理器。":u8"复制失败，请重试。";
    if(copy_label)host->ui.set_text(host->host_context,copy_label,view(message));
}
San14Result SAN14_MOD_CALL open_page(void*){
    if(!host)return SAN14_E_UNSUPPORTED;
    const auto closed=close_panel();if(closed!=SAN14_OK)return closed;
    if(active||stopping)refresh();
    const bool styled=host->struct_size>=offsetof(San14HostApi,ui_style)+sizeof(host->ui_style)&&host->ui_style&&
        host->ui_style->struct_size>=sizeof(San14UiStyleApi)&&host->ui_style->bind_help;
    const San14PanelSpec spec{sizeof(spec),view(u8"MCP"),1920,1080,0,0,0,0};
    auto result=host->ui.create_panel(host->host_context,&spec,&panel);if(result!=SAN14_OK)return result;
    auto label=[&](const char* id,const char* text,int x,int y,int width,int height,uint32_t flags,San14ControlHandle* out){
        const San14ControlSpec control{sizeof(control),view(id),view(text),x,y,width,height,flags};
        return host->ui.add_label(host->host_context,panel,&control,out);
    };
    San14ControlHandle help=0,temporary=0;
    result=label("mcp-preparation",mcp_help::preparation,360,112,1200,40,0,&temporary);
    if(result==SAN14_OK)result=label("mcp-help","",384,SAN14_EXPERT_HELP_TOP+18,1152,100,0,&help);
    auto row=[&](const char* id,const char* text,int y,const char* description,San14ControlHandle* out,bool with_copy=false){
        if(result!=SAN14_OK)return;
        result=label(id,"",360,y,1200,54,styled?SAN14_CONTROL_EXPERT_ROW:SAN14_CONTROL_HEADING,&temporary);
        if(result==SAN14_OK&&styled)result=host->ui_style->bind_help(host->host_context,temporary,help,view(description));
        const auto text_id=std::string(id)+"-text";
        if(result==SAN14_OK)result=label(text_id.c_str(),text,382,y+12,with_copy?900:1156,34,styled?SAN14_CONTROL_EXPERT_LABEL:0,out);
        if(result==SAN14_OK&&styled)result=host->ui_style->bind_help(host->host_context,*out,help,view(description));
    };
    auto copy_button=[&](const char* id,int y,int action,const char* caption,const char* description){
        if(result!=SAN14_OK)return;
        const San14ControlSpec spec{sizeof(spec),view(id),view(caption),1320,y+7,216,40,0};
        result=host->ui.add_button(host->host_context,panel,&spec,copy_text,&copy_actions[action],&temporary);
        if(result==SAN14_OK&&styled)result=host->ui_style->bind_help(host->host_context,temporary,help,view(description));
    };
    row("mcp-status",state_text(),174,u8"在模组管理中启用 MCP 即可启动服务；停用会关闭连接。主启用开关就是服务开关。",&status_label);
    const auto endpoint=mcp_help::endpoint_for(cached.port);
    row("mcp-endpoint",endpoint.c_str(),240,mcp_help::connection,&endpoint_label,true);
    copy_button("mcp-copy-endpoint",240,0,u8"复制地址",mcp_help::connection);
    row("mcp-install",u8"首次设置：把安装指令发给你的 AI 智能体",306,mcp_help::install,&temporary,true);
    copy_button("mcp-copy-install",306,1,u8"复制安装指令",mcp_help::install);
    for(int i=0;i<4;++i){
        const auto id=std::string("mcp-example-")+std::to_string(i);const int y=372+i*66;
        row(id.c_str(),mcp_help::prompts[i],y,mcp_help::prompts[i],&temporary,true);
        copy_button((id+"-copy").c_str(),y,i+2,u8"复制指令",mcp_help::prompts[i]);
    }
    row("mcp-usage",u8"使用说明：先载入存档并进入大地图，再开始聊天",636,mcp_help::usage,&temporary);
    if(result==SAN14_OK)result=label("mcp-error",cached.last_error,382,706,1156,42,0,&error_label);
    if(result==SAN14_OK)result=label("mcp-copy-feedback","",360,914,1200,30,0,&copy_label);
    if(result==SAN14_OK){
        const San14ControlSpec button{sizeof(button),view("mcp-back"),view(u8"返回"),904,SAN14_EXPERT_FOOTER_TOP,112,112,styled?SAN14_CONTROL_CONFIRM:0};
        result=host->ui.add_button(host->host_context,panel,&button,back,nullptr,&temporary);
    }
    if(result==SAN14_OK)result=host->ui.show_panel(host->host_context,panel);
    if(result!=SAN14_OK){close_panel();return result;}paint();return SAN14_OK;
}
San14Result SAN14_MOD_CALL load(San14ModContext*){
    const San14SettingsPageSpec page{sizeof(page),open_page,nullptr};
    const auto result=host->pages->register_page(host->host_context,&page);
    if(result==SAN14_OK)page_registered=true;return result;
}
San14Result SAN14_MOD_CALL enable(San14ModContext*){
    if(!available())return SAN14_E_UNSUPPORTED;
    if(active||stopping)return SAN14_E_BUSY;
    const auto subscribed=host->events.subscribe(host->host_context,SAN14_EVENT_UI_IDLE,0,idle,nullptr,&idle_subscription);
    if(subscribed!=SAN14_OK)return subscribed;
    const San14McpConfig config{sizeof(config),SAN14_MCP_DEFAULT_PORT};
    const auto result=host->mcp->start(host->host_context,&config,&start_task);
    if(result!=SAN14_OK){host->events.unsubscribe(host->host_context,idle_subscription);idle_subscription=0;start_task=0;return result;}
    active=true;stopping=stop_requested=false;last_refresh=0;
    cached={sizeof(cached),SAN14_MCP_STARTING,SAN14_MCP_DEFAULT_PORT,{}};
    log(SAN14_LOG_INFO,"MCP listener start queued for 127.0.0.1:8765");paint();return SAN14_OK;
}
San14Result SAN14_MOD_CALL disable(San14ModContext*){
    if(active||stopping){
        stopping=true;
        if(!stop_requested){
            const auto result=host->mcp->stop(host->host_context,&stop_task);
            if(result!=SAN14_OK){paint();return result;}
            stop_requested=true;
        }
        const auto status=refresh();
        if(status==SAN14_OK&&!stop_task&&cached.state==SAN14_MCP_FAILED){stop_requested=false;paint();return SAN14_E_PLUGIN_FAILED;}
        if(status!=SAN14_OK||stop_task||cached.state!=SAN14_MCP_STOPPED){paint();return SAN14_E_BUSY;}
        if(!release_task(start_task))return SAN14_E_BUSY;
    }
    if(idle_subscription){
        const auto result=host->events.unsubscribe(host->host_context,idle_subscription);
        if(result!=SAN14_OK&&result!=SAN14_E_NOT_FOUND)return result;
        idle_subscription=0;
    }
    const auto closed=close_panel();if(closed!=SAN14_OK)return closed;
    active=stopping=stop_requested=false;cached={sizeof(cached),SAN14_MCP_STOPPED,SAN14_MCP_DEFAULT_PORT,{}};
    log(SAN14_LOG_INFO,"MCP listener stopped");return SAN14_OK;
}
void SAN14_MOD_CALL unload(San14ModContext*){
    // Host must not unload an owner whose disable is still BUSY.
    if(active||stopping)return;
    close_panel();if(page_registered){host->pages->unregister_page(host->host_context);page_registered=false;}
    host=nullptr;
}
}
extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi* value,San14ModApi* output){
    if(!value||!output||output->struct_size<sizeof(*output)||value->abi_version!=SAN14_HOST_ABI_V1||
       value->struct_size<offsetof(San14HostApi,mcp)+sizeof(value->mcp)||
       !value->pages||value->pages->struct_size<sizeof(San14PagesApi)||!value->pages->register_page||!value->pages->unregister_page||
       value->ui.struct_size<sizeof(San14UiApi)||!value->ui.create_panel||!value->ui.add_label||!value->ui.add_button||!value->ui.set_text||!value->ui.show_panel||!value->ui.close_panel||
       value->events.struct_size<sizeof(San14EventApi)||!value->events.subscribe||!value->events.unsubscribe)return SAN14_E_UNSUPPORTED;
    if(active||stopping)return SAN14_E_BUSY;
    host=value;if(!available()){host=nullptr;return SAN14_E_UNSUPPORTED;}
    *output={sizeof(*output),SAN14_HOST_ABI_V1,load,enable,disable,unload};return SAN14_OK;
}
