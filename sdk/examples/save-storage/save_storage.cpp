#include "san14_mod.h"
#include "san14_idle.h"
#include <array>
#include <cstring>
#include <limits>
#include <string>

namespace {
const San14HostApi* host=nullptr;
San14Subscription idle_subscription=0;
San14PanelHandle panel=0;
San14ControlHandle count_label=0,status_label=0,file_label=0;
bool active=false,page_registered=false,have_context=false,counter_valid=false;
uint64_t revision=0,counter=0;
uint32_t state=SAN14_STORAGE_UNAVAILABLE;
std::string status_text,save_name,last_count,last_status,last_file;
San14StringView text(const char* value){return {value,static_cast<uint32_t>(std::strlen(value))};}
San14StringView text(const std::string& value){return {value.data(),static_cast<uint32_t>(value.size())};}
template<size_t N>std::string bounded_text(const char (&value)[N]){size_t size=0;while(size<N&&value[size])++size;return {value,size};}
void discard_cache(){counter_valid=false;counter=0;have_context=false;revision=0;state=SAN14_STORAGE_UNAVAILABLE;save_name.clear();}
void forget_panel(){panel=0;count_label=0;status_label=0;file_label=0;last_count.clear();last_status.clear();last_file.clear();}
San14Result close_panel(){
    if(!panel)return SAN14_OK;const auto result=host->ui.close_panel(host->host_context,panel);
    if(result==SAN14_OK||result==SAN14_E_NOT_FOUND){forget_panel();return SAN14_OK;}return result;
}
void label(San14ControlHandle handle,const std::string& value,std::string& previous){
    if(!handle||value==previous)return;
    const auto result=host->ui.set_text(host->host_context,handle,text(value));
    if(result==SAN14_OK)previous=value;else if(result==SAN14_E_NOT_FOUND)forget_panel();
}
void render(){
    if(!panel)return;
    label(count_label,counter_valid?u8"本档计数："+std::to_string(counter):u8"本档计数：不可用",last_count);
    label(status_label,status_text,last_status);
    label(file_label,save_name.empty()?u8"存档：尚未保存的新局，或当前没有可用存档":u8"存档："+save_name,last_file);
}
void refresh(bool force=false){
    San14SaveStorageInfo info{};info.struct_size=sizeof(info);
    const auto result=host->save_storage->context(host->host_context,&info);
    if(result!=SAN14_OK){discard_cache();status_text=u8"无法读取存储上下文，错误码："+std::to_string(result);render();return;}
    const bool switched=!have_context||revision!=info.revision||state!=info.state;
    if(switched){
        // Never carry a counter from the old world into a newly loaded save.
        discard_cache();have_context=true;revision=info.revision;state=info.state;
    }
    save_name=bounded_text(info.save_basename);
    if(!switched&&!force){render();return;}
    counter_valid=false;counter=0;
    if(state!=SAN14_STORAGE_READY){
        status_text=state==SAN14_STORAGE_LOADING?u8"正在读档，请稍候。":state==SAN14_STORAGE_ERROR?u8"附加数据不可用，请检查管理器日志。":u8"请进入新局或读取存档。";render();return;
    }
    // A versioned, fixed-size little-endian payload avoids compiler padding.
    std::array<unsigned char,12> payload{};uint32_t required=0;
    const auto read=host->storage.read(host->host_context,text("save"),text("counter"),payload.data(),static_cast<uint32_t>(payload.size()),&required);
    if(read==SAN14_E_NOT_FOUND){counter=0;counter_valid=true;status_text=u8"此档尚无计数；点击增加后，正常保存游戏即可持久化。";}
    else if(read==SAN14_OK&&required==payload.size()&&std::memcmp(payload.data(),"CNT1",4)==0){
        for(unsigned i=0;i<8;++i)counter|=uint64_t(payload[4+i])<<(i*8);
        counter_valid=true;status_text=u8"已读取本档计数；读回旧档时计数会一起回退。";
    }else{
        // Corrupt/unknown data and read errors are not a missing counter. Do not
        // turn them into zero and overwrite data belonging to a newer version.
        status_text=read==SAN14_OK?u8"计数数据格式不受支持，已停止写入。":u8"计数读取失败，未覆盖数据。错误码："+std::to_string(read);
    }
    render();
}
void SAN14_MOD_CALL increase(void*,San14ControlHandle){
    if(!active)return;refresh(true);if(!counter_valid)return;
    if(counter==std::numeric_limits<uint64_t>::max()){status_text=u8"计数已达到上限。";render();return;}
    const auto next=counter+1;std::array<unsigned char,12> payload{{'C','N','T','1'}};
    for(unsigned i=0;i<8;++i)payload[4+i]=static_cast<unsigned char>(next>>(i*8));
    const auto written=host->storage.write_atomic(host->host_context,text("save"),text("counter"),payload.data(),static_cast<uint32_t>(payload.size()));
    if(written==SAN14_OK){counter=next;status_text=u8"已更新当前局数据；正常保存游戏后写入附加文件。";}
    else{counter_valid=false;status_text=u8"写入失败，计数未增加。错误码："+std::to_string(written);}
    render();
}
void SAN14_MOD_CALL close(void*,San14ControlHandle){close_panel();}
San14Result SAN14_MOD_CALL open(void*){
    if(!active)return SAN14_E_PERMISSION_DENIED;
    auto result=close_panel();if(result!=SAN14_OK)return result;refresh(true);
    const San14PanelSpec spec{sizeof(spec),text(u8"存档附加数据示例"),740,320,0,0,0,0};
    result=host->ui.create_panel(host->host_context,&spec,&panel);if(result!=SAN14_OK)return result;
    auto add_label=[](const char* id,int y,San14ControlHandle& output){const San14ControlSpec value{sizeof(value),text(id),text(""),24,y,690,32,0};return host->ui.add_label(host->host_context,panel,&value,&output);};
    result=add_label("counter",65,count_label);if(result==SAN14_OK)result=add_label("status",112,status_label);if(result==SAN14_OK)result=add_label("save",159,file_label);
    const San14ControlSpec add{sizeof(add),text("increment"),text(u8"计数 +1"),24,226,220,42,0};San14ControlHandle button=0;
    if(result==SAN14_OK)result=host->ui.add_button(host->host_context,panel,&add,increase,nullptr,&button);
    const San14ControlSpec done{sizeof(done),text("close"),text(u8"关闭"),490,226,220,42,0};
    if(result==SAN14_OK)result=host->ui.add_button(host->host_context,panel,&done,close,nullptr,&button);
    if(result==SAN14_OK){render();result=host->ui.show_panel(host->host_context,panel);}
    if(result!=SAN14_OK)close_panel();return result;
}
void SAN14_MOD_CALL idle(void*,const San14EventView* event){if(active&&event&&event->struct_size>=sizeof(San14EventView)&&event->event_id==SAN14_EVENT_UI_IDLE)refresh();}
San14Result SAN14_MOD_CALL on_load(San14ModContext*){
    // on_load registers presentation only; a world need not exist yet.
    const San14SettingsPageSpec spec{sizeof(spec),open,nullptr};const auto result=host->pages->register_page(host->host_context,&spec);page_registered=result==SAN14_OK;return result;
}
San14Result SAN14_MOD_CALL on_enable(San14ModContext*){
    discard_cache();const auto result=host->events.subscribe(host->host_context,SAN14_EVENT_UI_IDLE,0,idle,nullptr,&idle_subscription);
    if(result!=SAN14_OK)return result;active=true;refresh(true);return SAN14_OK;
}
San14Result SAN14_MOD_CALL on_disable(San14ModContext*){
    active=false;bool busy=close_panel()!=SAN14_OK;
    if(idle_subscription){const auto result=host->events.unsubscribe(host->host_context,idle_subscription);if(result==SAN14_OK||result==SAN14_E_NOT_FOUND)idle_subscription=0;else busy=true;}
    discard_cache();return busy?SAN14_E_BUSY:SAN14_OK;
}
void SAN14_MOD_CALL on_unload(San14ModContext*){if(page_registered)host->pages->unregister_page(host->host_context);page_registered=false;host=nullptr;}
bool supported(const San14HostApi* value){
    // Gate every optional tail and each table prefix before reading functions.
    if(!value||value->struct_size<offsetof(San14HostApi,save_storage)+sizeof(value->save_storage)||value->abi_version!=SAN14_HOST_ABI_V1)return false;
    if(!value->save_storage||value->save_storage->struct_size<offsetof(San14SaveStorageApi,context)+sizeof(value->save_storage->context)||!value->save_storage->context)return false;
    if(!value->pages||value->pages->struct_size<offsetof(San14PagesApi,unregister_page)+sizeof(value->pages->unregister_page)||!value->pages->register_page||!value->pages->unregister_page)return false;
    if(value->events.struct_size<offsetof(San14EventApi,unsubscribe)+sizeof(value->events.unsubscribe)||!value->events.subscribe||!value->events.unsubscribe)return false;
    if(value->storage.struct_size<offsetof(San14StorageApi,write_atomic)+sizeof(value->storage.write_atomic)||!value->storage.read||!value->storage.write_atomic)return false;
    return value->ui.struct_size>=offsetof(San14UiApi,close_panel)+sizeof(value->ui.close_panel)&&value->ui.create_panel&&value->ui.add_label&&value->ui.add_button&&value->ui.set_text&&value->ui.show_panel&&value->ui.close_panel;
}
}
extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi* value,San14ModApi* output){
    if(!output||output->struct_size<sizeof(San14ModApi)||!supported(value))return SAN14_E_UNSUPPORTED;
    host=value;San14ModApi api{};api.struct_size=sizeof(api);api.abi_version=SAN14_HOST_ABI_V1;api.on_load=on_load;api.on_enable=on_enable;api.on_disable=on_disable;api.on_unload=on_unload;*output=api;return SAN14_OK;
}
