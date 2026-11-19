#pragma once
#include "plugin_ui.h"
#include <cassert>
#include <cstring>
#include <cwchar>
#include <set>
namespace plugin_ui_test {
San14StringView sv(const char* s) { return {s, static_cast<uint32_t>(std::strlen(s))}; }
struct NativeText { const wchar_t* value = nullptr; bool chosen=false;uint32_t tint=0;int x=0,y=0,w=0,h=0;uint32_t flags=0;bool enabled=true; };
struct NativeSlider { int x=0,y=0,w=0,h=0;int32_t minimum=0,maximum=0,value=0; };
struct NativePanel {
    int x = 0, y = 0, width = 0, height = 0;
    bool visible = false, themed=false, help_visible=false, ready=true;
    std::vector<std::unique_ptr<NativeText>> texts;
    std::vector<std::unique_ptr<NativeSlider>> sliders;
};
struct Fixture {
    bool ui_thread = true;
    bool fail_frame = false, fail_text = false, fail_set = false, fail_show = false, fail_slider = false;
    std::set<void*> allowed;
    std::map<void*, std::unique_ptr<NativePanel>> panels;
    int created = 0, destroyed = 0, callbacks = 0,text_callbacks = 0, int_callbacks = 0;
    std::vector<std::pair<void*,bool>> visibility_events;
    static Fixture& self(void* p) { return *static_cast<Fixture*>(p); }
    static bool ui(void* p) { return self(p).ui_thread; }
    static bool allow(void* p, void* owner) { return self(p).allowed.count(owner) != 0; }
    static void* create(void* p, int x, int y, int w, int h) {
        auto& f = self(p); auto n = std::make_unique<NativePanel>();
        n->x=x; n->y=y; n->width=w; n->height=h;
        auto key=n.get();f.panels.emplace(key,std::move(n));++f.created;return key;
    }
    static bool frame(void* p, void*, int, int, int, int) {
        if (self(p).fail_frame) { self(p).fail_frame=false;return false; }return true;
    }
    static void* text(void* p, void* parent, int x, int y, int w, int h, const wchar_t* value, int, int) {
        auto& f=self(p);if(f.fail_text){f.fail_text=false;return nullptr;}
        auto n=std::make_unique<NativeText>();n->value=value;n->x=x;n->y=y;n->w=w;n->h=h;auto key=n.get();
        f.panels.at(parent)->texts.push_back(std::move(n));return key;
    }
    static bool set(void* p, void* native, const wchar_t* value) {
        auto& f=self(p);if(f.fail_set){f.fail_set=false;return false;}
        static_cast<NativeText*>(native)->value=value;return true;
    }
    static bool visible(void* p, void* native, bool value) {
        auto& f=self(p);if(f.fail_show&&value){f.fail_show=false;return false;}
        f.panels.at(native)->visible=value;f.visibility_events.push_back({native,value});return true;
    }
    static bool prepare(void* p,void* native){return self(p).panels.at(native)->ready;}
    static void destroy(void* p, void* native) {
        auto& f=self(p);
        // Backend may retain pointers; all backing text must still be valid here.
        for(const auto& t:f.panels.at(native)->texts)assert(t->value&&std::wcslen(t->value)<=plugin_ui::Runtime::max_text_bytes);
        assert(f.panels.erase(native)==1);++f.destroyed;
    }
    static void invoke(void* p,San14ControlCallback callback,void* context,San14ControlHandle handle) {
        ++self(p).callbacks;callback(context,handle);
    }
    static void invoke_text(void* p,San14TextChangedCallback callback,void* context,San14ControlHandle handle,San14StringView value) {
        ++self(p).text_callbacks;callback(context,handle,value);
    }
    plugin_ui::Backend backend() { return {this,ui,allow,create,frame,text,set,visible,destroy,invoke,invoke_text}; }
    static bool shell(void* p,void* parent,const wchar_t* title){self(p).panels.at(parent)->themed=true;return text(p,parent,0,0,1,1,title,24,9)!=nullptr;}
    static void help_visible(void* p,void* parent,bool value){self(p).panels.at(parent)->help_visible=value;}
    static void* styled(void* p,void* parent,int x,int y,int w,int h,const wchar_t* value,uint32_t flags){
        auto c=static_cast<NativeText*>(text(p,parent,x,y,w,h,value,18,9));if(c){c->chosen=(flags&SAN14_CONTROL_SELECTED)!=0;c->x=x;c->y=y;c->w=w;c->h=h;c->flags=flags;}return c;
    }
    static bool selected(void*,void* control,bool value){static_cast<NativeText*>(control)->chosen=value;return true;}
    static bool enabled(void*,void* control,bool value){static_cast<NativeText*>(control)->enabled=value;return true;}
    static bool tint(void*,void* control,uint32_t argb){static_cast<NativeText*>(control)->tint=argb;return true;}
    static void* slider_create(void* p,void* parent,int x,int y,int w,int h,int32_t minimum,int32_t maximum,int32_t value){
        auto& f=self(p);if(f.fail_slider){f.fail_slider=false;return nullptr;}
        auto s=std::make_unique<NativeSlider>(NativeSlider{x,y,w,h,minimum,maximum,value});
        auto key=s.get();f.panels.at(parent)->sliders.push_back(std::move(s));return key;
    }
    static bool slider_update(void*,void* control,int32_t minimum,int32_t maximum,int32_t value){
        auto* s=static_cast<NativeSlider*>(control);s->minimum=minimum;s->maximum=maximum;s->value=value;return true;
    }
    static void invoke_int(void* p,San14IntChangedCallback callback,void* context,San14ControlHandle handle,int32_t value){
        ++self(p).int_callbacks;callback(context,handle,value);
    }
    plugin_ui::Backend styled_backend(){
        auto b=backend();b.heading=frame;b.expert_shell=shell;b.styled_button=styled;b.selected=selected;b.help_visible=help_visible;b.tint=tint;
        b.slider_create=slider_create;b.slider_update=slider_update;b.invoke_int=invoke_int;b.enabled=enabled;return b;
    }
    NativePanel& only_panel() {assert(panels.size()==1);return *panels.begin()->second;}
};
}
