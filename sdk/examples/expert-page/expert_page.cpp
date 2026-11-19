#include "san14_mod.h"
#include "san14_expert_page.h"
#include <cstring>
#include <memory>

namespace {
const San14HostApi* host=nullptr;
std::unique_ptr<san14::ExpertPage> page;
bool enabled=false;
int32_t amount=4;
San14StringView sv(const char* text){return {text,static_cast<uint32_t>(std::strlen(text))};}
San14Result save(const char* key,int32_t value){
    return host->storage.write_atomic(host->host_context,sv("global"),sv(key),&value,sizeof(value));
}
int32_t read(const char* key,int32_t fallback){
    int32_t value=fallback;uint32_t required=0;
    return host->storage.read(host->host_context,sv("global"),sv(key),&value,sizeof(value),&required)==SAN14_OK&&required==sizeof(value)?value:fallback;
}
San14Result SAN14_MOD_CALL open(void*){
    try{
        if(page)page->close();
        enabled=read("enabled",0)!=0;amount=std::clamp(read("amount",4),0,100);
        page=std::make_unique<san14::ExpertPage>(host,u8"原版风格页面示例");
        page->add_bool(u8"规则","enabled",u8"启用示例",u8"鼠标指向此行时，在下方显示说明。写入成功后才更新选项。这个示例不会修改游戏规则。",enabled,
            [](int32_t value){auto result=save("enabled",value);if(result==SAN14_OK)enabled=value!=0;return result;});
        page->add_integer(u8"数值","amount",u8"示例数值",u8"使用减号、加号调整数值；标准值为 4。此示例仅保存插件设置。",amount,0,100,4,
            [](int32_t value){auto result=save("amount",value);if(result==SAN14_OK)amount=value;return result;});
        return page->show();
    }catch(...){return SAN14_E_PLUGIN_FAILED;}
}
San14Result SAN14_MOD_CALL load(San14ModContext*){
    San14SettingsPageSpec spec{sizeof(spec),open,nullptr};return host->pages->register_page(host->host_context,&spec);
}
San14Result SAN14_MOD_CALL activate(San14ModContext*){return SAN14_OK;}
San14Result SAN14_MOD_CALL deactivate(San14ModContext*){return page?page->close():SAN14_OK;}
void SAN14_MOD_CALL unload(San14ModContext*){page.reset();}
}
SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(const San14HostApi* h,San14ModApi* out){
    if(!out||out->struct_size<sizeof(*out)||!san14::ExpertPage::supported(h)||!h->pages||!h->pages->register_page)return SAN14_E_UNSUPPORTED;
    host=h;*out={sizeof(*out),SAN14_HOST_ABI_V1,load,activate,deactivate,unload};return SAN14_OK;
}
