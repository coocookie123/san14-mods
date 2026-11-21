#ifndef SAN14_MOD_SDK_EXPERT_PAGE_H
#define SAN14_MOD_SDK_EXPERT_PAGE_H
#include "san14_host.h"
#include "san14_settings_template.h"
#ifdef __cplusplus
#include <algorithm>
#include <functional>
#include <memory>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace san14 {
/* Header-only C++17 builder. Keep Page alive until close(); use on the UI thread.
 * All text and callbacks are owned by Page. Changes commit only after the
 * callback returns SAN14_OK. Storage/rules remain the plugin's responsibility.
 * C plugins can compose the same layout using ui + ui_style directly. */
class ExpertPage {
public:
    using Change = std::function<San14Result(int32_t)>;
    explicit ExpertPage(const San14HostApi* host,std::string title):host_(host),title_(std::move(title)){}
    ExpertPage(const ExpertPage&)=delete;
    ExpertPage& operator=(const ExpertPage&)=delete;
    ~ExpertPage(){close();}
    static bool supported(const San14HostApi* h){
        return h&&h->struct_size>=offsetof(San14HostApi,ui_style)+sizeof(h->ui_style)&&h->ui_style&&
            h->ui_style->struct_size>=offsetof(San14UiStyleApi,bind_help)+sizeof(h->ui_style->bind_help)&&
            (h->ui_style->capabilities&SAN14_UI_STYLE_EXPERT)&&
            h->ui_style->set_selected&&h->ui_style->bind_help;
    }
    void add_bool(std::string category,std::string key,std::string label,std::string help,bool value,Change changed){
        add({std::move(category),std::move(key),std::move(label),std::move(help),Kind::Boolean,value?1:0,0,1,0,1,1,{},std::move(changed)});
    }
    /* Integer stepper with standard-value button. This is not the game's native
     * scenario slider and does not read or modify scenario settings. */
    void add_integer(std::string category,std::string key,std::string label,std::string help,
                     int32_t value,int32_t minimum,int32_t maximum,int32_t standard,Change changed){
        if(minimum>maximum||value<minimum||value>maximum||standard<minimum||standard>maximum)throw SAN14_E_INVALID_ARGUMENT;
        add({std::move(category),std::move(key),std::move(label),std::move(help),Kind::Integer,value,minimum,maximum,standard,1,1,{},std::move(changed)});
    }
    void add_slider(std::string category,std::string key,std::string label,std::string help,
                    int32_t value,int32_t minimum,int32_t maximum,int32_t step,
                    int32_t display_divisor,std::string unit_suffix,Change changed){
        if(minimum>=maximum||step<=0||display_divisor<=0)throw SAN14_E_INVALID_ARGUMENT;
        const int32_t clamped=snap_range_value(value,minimum,maximum,step);
        add({std::move(category),std::move(key),std::move(label),std::move(help),Kind::Slider,clamped,minimum,maximum,minimum,step,display_divisor,std::move(unit_suffix),std::move(changed)});
    }
    void add_action(std::string category,std::string key,std::string label,std::string help,std::function<San14Result()> action,std::string caption=u8"打开"){
        action_captions_[key]=std::move(caption);
        add({std::move(category),std::move(key),std::move(label),std::move(help),Kind::Action,0,0,0,0,1,1,{},
            [action=std::move(action)](int32_t){return action?action():SAN14_E_INVALID_ARGUMENT;}});
    }
    // A noninteractive row for a feature that is not yet available.
    void add_unavailable(std::string category,std::string key,std::string label,std::string help){
        add({std::move(category),std::move(key),std::move(label),std::move(help),Kind::Unavailable,0,0,0,0,1,1,{},{}});
    }
    San14Result show(){if(!supported(host_))return SAN14_E_UNSUPPORTED;return render();}
    San14Result close(){
        close_input();
        if(!panel_)return SAN14_OK;
        auto result=host_->ui.close_panel(host_->host_context,panel_);
        if(result==SAN14_OK||result==SAN14_E_NOT_FOUND){panel_=0;footer_label_=0;footer_hint_label_=0;footer_controls_.clear();bindings_.clear();slider_views_.clear();row_views_.clear();return SAN14_OK;}
        return result;
    }
    San14PanelHandle handle()const{return panel_;}
    // Update dependent rows without invoking their change callbacks. Call
    // refresh() after a selector changes; the current tab remains selected.
    void update_range(const std::string& key,int32_t value,int32_t standard,std::string help){
        for(auto& row:rows_)if(row.key==key){
            if(value<row.minimum||value>row.maximum||standard<row.minimum||standard>row.maximum)throw SAN14_E_INVALID_ARGUMENT;
            row.value=value;row.standard=standard;row.help=std::move(help);return;
        }
        throw SAN14_E_NOT_FOUND;
    }
    void update_bool(const std::string& key,bool value){
        for(auto& row:rows_)if(row.key==key&&row.kind==Kind::Boolean){row.value=value?1:0;return;}
    }
    void action_caption(const std::string& key,std::string caption){action_captions_[key]=std::move(caption);}
    // Color the native edit button's own text. Older Hosts keep a readable
    // stock caption; never put a panel label underneath a button controller.
    void set_range_changed(const std::string& key,bool changed){
        auto previous=range_changed_.find(key);
        if(previous!=range_changed_.end()&&previous->second==changed)return;
        range_changed_[key]=changed;
        for(size_t i=0;i<rows_.size();++i)if(rows_[i].key==key){tint_range(i);break;}
    }
    void set_footer_actions(std::vector<std::pair<std::string,std::function<San14Result()>>> actions){
        if(panel_||actions.size()>2)throw SAN14_E_INVALID_ARGUMENT;footer_actions_=std::move(actions);
    }
    void footer_hint(std::string hint){
        footer_hint_=std::move(hint);
        if(footer_hint_label_)host_->ui.set_text(host_->host_context,footer_hint_label_,sv(footer_hint_));
    }
    void footer_status(std::string message,bool pending){
        footer_message_=std::move(message);footer_pending_=pending;
        if(footer_label_)host_->ui.set_text(host_->host_context,footer_label_,sv(footer_message_));
        for(size_t i=0;i<footer_controls_.size();++i)apply_footer_enabled(i);
    }
    void footer_action_enabled(size_t index,bool enabled){
        footer_action_disabled_[index]=!enabled;
        apply_footer_enabled(index);
    }
    San14Result refresh(){
        if(!panel_)return SAN14_OK;
        try{for(auto& entry:row_views_)refresh_row(entry.first,entry.second);return SAN14_OK;}
        catch(San14Result result){return result;}catch(...){return SAN14_E_PLUGIN_FAILED;}
    }
private:
    enum class Kind{Boolean,Integer,Slider,Action,Unavailable};
    enum class Command{Change,Edit,CommitInput,CancelInput,Tab,Page,Close,Footer};
    struct Row{std::string category,key,label,help;Kind kind;int32_t value,minimum,maximum,standard,step,divisor;std::string suffix;Change change;};
    struct Binding{ExpertPage* page;Command command;size_t index;int32_t value;};
    struct SliderView{San14ControlHandle slider=0,readout=0;};
    struct RowView{
        San14ControlHandle readout=0,action=0,tinted_value=0;
        bool button_text=false;
        Binding *decrease=nullptr,*increase=nullptr,*standard=nullptr;
        std::vector<San14ControlHandle> help_sources;
    };
    const San14HostApi* host_;std::string title_;std::vector<Row> rows_;
    San14PanelHandle panel_=0;San14ControlHandle help_=0;
    San14PanelHandle input_panel_=0;std::string input_text_;
    size_t tab_=0,first_=0;std::vector<std::unique_ptr<Binding>> bindings_,input_bindings_;
    std::map<size_t,std::pair<San14ControlHandle,San14ControlHandle>> choices_;
    std::map<size_t,SliderView> slider_views_;
    std::map<size_t,RowView> row_views_;
    std::map<std::string,std::string> action_captions_;
    std::map<std::string,bool> range_changed_;
    std::vector<std::pair<std::string,std::function<San14Result()>>> footer_actions_;
    std::vector<San14ControlHandle> footer_controls_;
    std::map<size_t,bool> footer_action_disabled_;
    San14ControlHandle footer_label_=0,footer_hint_label_=0;
    std::string footer_message_,footer_hint_;bool footer_pending_=false;
    bool footer_action_is_enabled(size_t index)const{
        if(!footer_pending_)return false;
        const auto it=footer_action_disabled_.find(index);
        return it==footer_action_disabled_.end()||!it->second;
    }
    void apply_footer_enabled(size_t index){
        if(index>=footer_controls_.size())return;
        if(host_->ui_style->struct_size>=offsetof(San14UiStyleApi,set_enabled)+sizeof(host_->ui_style->set_enabled)&&host_->ui_style->set_enabled)
            host_->ui_style->set_enabled(host_->host_context,footer_controls_[index],footer_action_is_enabled(index)?1:0);
    }
    static bool has_slider_api(const San14HostApi* h){
        return h&&h->struct_size>=offsetof(San14HostApi,ui_slider)+sizeof(h->ui_slider)&&h->ui_slider&&
            h->ui_slider->struct_size>=offsetof(San14UiSliderApi,get_slider_value)+sizeof(h->ui_slider->get_slider_value)&&
            h->ui_slider->add_slider&&h->ui_slider->set_slider_value;
    }
    static bool has_input_api(const San14HostApi* h){
        return h&&h->struct_size>=offsetof(San14HostApi,ui_extended)+sizeof(h->ui_extended)&&
            h->ui_extended.struct_size>=offsetof(San14UiExtendedApi,add_text_input)+sizeof(h->ui_extended.add_text_input)&&
            h->ui_extended.add_text_input;
    }
    bool has_tint_api()const{
        return host_->ui_style&&(host_->ui_style->capabilities&SAN14_UI_STYLE_TINT)&&
            host_->ui_style->struct_size>=offsetof(San14UiStyleApi,set_tint)+sizeof(host_->ui_style->set_tint)&&host_->ui_style->set_tint;
    }
    void tint_range(size_t index){
        if(!has_tint_api())return;auto view=row_views_.find(index);
        if(view==row_views_.end()||!view->second.tinted_value)return;
        const auto flag=range_changed_.find(rows_[index].key);
        const bool button=view->second.button_text;
        const uint32_t color=flag!=range_changed_.end()&&flag->second?(button?0xff1d63c7u:0xff60b8ffu):(button?0xff262018u:0xffffffffu);
        host_->ui_style->set_tint(host_->host_context,view->second.tinted_value,color);
    }
    static San14StringView sv(const std::string& s){return {s.data(),static_cast<uint32_t>(s.size())};}
    static void check(San14Result r){if(r!=SAN14_OK)throw r;}
    void add(Row row){
        if(panel_||row.key.empty()||rows_.size()>=256)throw SAN14_E_INVALID_ARGUMENT;
        for(auto& r:rows_)if(r.key==row.key)throw SAN14_E_INVALID_ARGUMENT;
        rows_.push_back(std::move(row));
    }
    void refresh_row(size_t index,RowView& view){
        const auto& row=rows_.at(index);
        if(auto c=choices_.find(index);c!=choices_.end()){
            check(host_->ui_style->set_selected(host_->host_context,c->second.first,row.value!=0));
            check(host_->ui_style->set_selected(host_->host_context,c->second.second,row.value==0));
        }
        if(auto s=slider_views_.find(index);s!=slider_views_.end()){
            check(host_->ui_slider->set_slider_value(host_->host_context,s->second.slider,row.value));
            check(host_->ui.set_text(host_->host_context,s->second.readout,sv(format_range_value(row.value,row.divisor,row.suffix))));
        }
        const int32_t step=row.kind==Kind::Integer?1:row.step;
        auto stepped=[&](int64_t candidate){auto v=static_cast<int32_t>(std::clamp(candidate,int64_t(row.minimum),int64_t(row.maximum)));return row.kind==Kind::Slider?snap_range_value(v,row.minimum,row.maximum,row.step):v;};
        if(view.decrease)view.decrease->value=stepped(int64_t(row.value)-step);
        if(view.increase)view.increase->value=stepped(int64_t(row.value)+step);
        if(view.standard)view.standard->value=row.standard;
        if(view.readout)check(host_->ui.set_text(host_->host_context,view.readout,sv(row.kind==Kind::Integer?std::to_string(row.value):format_range_value(row.value,row.divisor,row.suffix))));
        if(view.action)check(host_->ui.set_text(host_->host_context,view.action,sv(action_captions_[row.key])));
        tint_range(index);
        for(auto source:view.help_sources)check(host_->ui_style->bind_help(host_->host_context,source,help_,sv(row.help)));
    }
    static void apply_value(ExpertPage* p,size_t index,int32_t candidate,bool from_slider){
        if(!p||index>=p->rows_.size())return;
        auto& row=p->rows_[index];
        const auto kind=row.kind;
        if(kind==Kind::Unavailable)return;
        if(kind!=Kind::Action&&candidate==row.value)return;
        auto callback=row.change;
        const auto result=callback?callback(candidate):SAN14_E_INVALID_ARGUMENT;
        if(result!=SAN14_OK){
            if(from_slider&&has_slider_api(p->host_)){
                auto sv_it=p->slider_views_.find(index);
                if(sv_it!=p->slider_views_.end()&&sv_it->second.slider)p->host_->ui_slider->set_slider_value(p->host_->host_context,sv_it->second.slider,row.value);
            }
            auto message=std::string(u8"未能应用此设置（")+std::to_string(result)+u8"），原值已保留。";
            p->host_->ui.set_text(p->host_->host_context,p->help_,sv(message));return;
        }
        row.value=candidate;
        if(kind==Kind::Boolean){auto i=p->choices_.find(index);if(i!=p->choices_.end()){
            p->host_->ui_style->set_selected(p->host_->host_context,i->second.first,candidate!=0);
            p->host_->ui_style->set_selected(p->host_->host_context,i->second.second,candidate==0);}
        }else if(kind==Kind::Slider){
            auto i=p->slider_views_.find(index);
            if(i!=p->slider_views_.end()){
                if(!from_slider&&has_slider_api(p->host_)&&i->second.slider)p->host_->ui_slider->set_slider_value(p->host_->host_context,i->second.slider,candidate);
                if(i->second.readout)p->host_->ui.set_text(p->host_->host_context,i->second.readout,sv(format_range_value(candidate,row.divisor,row.suffix)));
            }else if(p->panel_){auto v=p->row_views_.find(index);if(v!=p->row_views_.end())p->refresh_row(index,v->second);}
        }else if(kind==Kind::Integer&&p->panel_){auto v=p->row_views_.find(index);if(v!=p->row_views_.end())p->refresh_row(index,v->second);}
    }
    static void SAN14_MOD_CALL slider_changed(void* context,San14ControlHandle,int32_t value){
        const auto binding=*static_cast<Binding*>(context);
        try{apply_value(binding.page,binding.index,value,true);}catch(...){}
    }
    static void SAN14_MOD_CALL input_changed(void* context,San14ControlHandle,San14StringView value){
        auto* p=static_cast<ExpertPage*>(context);
        if(p&&value.data&&value.length)p->input_text_.assign(value.data,value.length);
        else if(p)p->input_text_.clear();
    }
    void close_input(){
        if(input_panel_&&host_&&host_->ui.close_panel)host_->ui.close_panel(host_->host_context,input_panel_);
        input_panel_=0;input_bindings_.clear();
    }
    void open_input(size_t index){
        if(index>=rows_.size())return;
        const auto& row=rows_[index];
        if(!has_input_api(host_)){apply_value(this,index,row.standard,false);return;}
        close_input();
        San14PanelHandle modal=0;std::vector<std::unique_ptr<Binding>> modal_bindings;
        try{
            San14PanelSpec spec{sizeof(spec),sv(row.label),480,224,0};
            check(host_->ui.create_panel(host_->host_context,&spec,&modal));
            const std::string hint_id="hint",val_id="value";
            const std::string range_hint=std::string(u8"输入数值（")+format_range_value(row.minimum,row.divisor,row.suffix)+
                " ~ "+format_range_value(row.maximum,row.divisor,row.suffix)+u8"）";
            San14ControlSpec hs{sizeof(hs),sv(hint_id),sv(range_hint),24,56,432,26,0};
            San14ControlHandle hint=0;
            check(host_->ui.add_label(host_->host_context,modal,&hs,&hint));
            input_text_=format_range_value(row.value,row.divisor,"");
            San14ControlSpec is{sizeof(is),sv(val_id),sv(input_text_),24,90,432,40,0};
            San14ControlHandle input_ctrl=0;
            check(host_->ui_extended.add_text_input(host_->host_context,modal,&is,64,input_changed,this,&input_ctrl));
            auto add_btn=[&](int x,int y,int w,int h,const std::string& cap,Command cmd){
                const std::string bid=cmd==Command::CommitInput?"ok":"cancel";
                San14ControlSpec bs{sizeof(bs),sv(bid),sv(cap),x,y,w,h,0};
                auto b=std::make_unique<Binding>(Binding{this,cmd,index,0});auto ptr=b.get();modal_bindings.push_back(std::move(b));
                San14ControlHandle bh=0;
                check(host_->ui.add_button(host_->host_context,modal,&bs,click,ptr,&bh));
            };
            add_btn(56,154,160,40,u8"确定",Command::CommitInput);
            add_btn(264,154,160,40,u8"取消",Command::CancelInput);
            check(host_->ui.show_panel(host_->host_context,modal));
            if(host_->ui_extended.focus_text_input)host_->ui_extended.focus_text_input(host_->host_context,input_ctrl);
            if(host_->ui_extended.set_selection){
                San14TextSelection sel{sizeof(sel),0,static_cast<uint32_t>(input_text_.size())};
                host_->ui_extended.set_selection(host_->host_context,input_ctrl,&sel);
            }
            input_panel_=modal;input_bindings_=std::move(modal_bindings);
        }catch(...){if(modal)host_->ui.close_panel(host_->host_context,modal);}
    }
    static void SAN14_MOD_CALL click(void* context,San14ControlHandle){
        // Copy before any callback: a new page may destroy this binding.
        const auto binding=*static_cast<Binding*>(context);auto p=binding.page;
        try{
            if(binding.command==Command::Close){p->close();return;}
            if(binding.command==Command::Footer){
                if(!p->footer_action_is_enabled(binding.index)||binding.index>=p->footer_actions_.size())return;
                auto callback=p->footer_actions_[binding.index].second;if(callback)callback();return;
            }
            if(binding.command==Command::CancelInput){p->close_input();return;}
            if(binding.command==Command::CommitInput){
                if(binding.index<p->rows_.size()){
                    const auto& row=p->rows_[binding.index];
                    int32_t parsed=0;
                    const bool ok=parse_range_input(p->input_text_,row.minimum,row.maximum,row.step,row.divisor,parsed);
                    p->close_input();
                    if(ok)apply_value(p,binding.index,parsed,false);
                    else if(p->help_)p->host_->ui.set_text(p->host_->host_context,p->help_,sv(u8"请输入有效数值。"));
                }else p->close_input();
                return;
            }
            if(binding.command==Command::Edit){p->open_input(binding.index);return;}
            if(binding.command==Command::Tab||binding.command==Command::Page){
                if((binding.command==Command::Tab&&p->tab_==binding.index)||(binding.command==Command::Page&&p->first_==binding.index))return;
                auto old=p->tab_,old_first=p->first_;
                if(binding.command==Command::Tab){p->tab_=binding.index;p->first_=0;}else p->first_=binding.index;
                if(p->render()!=SAN14_OK){p->tab_=old;p->first_=old_first;}return;
            }
            apply_value(p,binding.index,binding.value,false);
        }catch(...){/* Never unwind across the plugin C ABI. */}
    }
    San14Result render(){
        San14PanelHandle next=0;std::vector<std::unique_ptr<Binding>> bindings;
        std::map<size_t,std::pair<San14ControlHandle,San14ControlHandle>> choices;
        std::map<size_t,SliderView> slider_views;
        std::map<size_t,RowView> row_views;
        try{
            auto pages=settings_categories(rows_);if(tab_>=pages.size())tab_=0;
            San14PanelSpec spec{sizeof(spec),sv(title_),SAN14_EXPERT_WIDTH,SAN14_EXPERT_HEIGHT,SAN14_PANEL_EXPERT};
            check(host_->ui.create_panel(host_->host_context,&spec,&next));
            unsigned id=0;
            auto label=[&](int x,int y,int w,int h,const std::string& value,uint32_t flags=0,const std::string& control_id=std::string{}){
                auto key=control_id.empty()?"label."+std::to_string(++id):control_id;San14ControlSpec s{sizeof(s),sv(key),sv(value),x,y,w,h,flags};San14ControlHandle out=0;
                check(host_->ui.add_label(host_->host_context,next,&s,&out));return out;};
            const auto help=label(SAN14_EXPERT_LEFT+24,SAN14_EXPERT_HELP_TOP+18,SAN14_EXPERT_ROW_WIDTH-48,100,"");
            RowView* current_view=nullptr;
            auto button=[&](int x,int y,int w,int h,const std::string& value,uint32_t flags,Command command,size_t index,int32_t number,const std::string& description,const std::string& control_id=std::string{}){
                auto key=control_id.empty()?"button."+std::to_string(++id):control_id;San14ControlSpec s{sizeof(s),sv(key),sv(value),x,y,w,h,flags};San14ControlHandle out=0;
                auto binding=std::make_unique<Binding>(Binding{this,command,index,number});auto pointer=binding.get();bindings.push_back(std::move(binding));
                check(host_->ui.add_button(host_->host_context,next,&s,click,pointer,&out));
                if(command==Command::Change||command==Command::Edit){check(host_->ui_style->bind_help(host_->host_context,out,help,sv(description)));if(current_view)current_view->help_sources.push_back(out);}return out;};
            // Six visible tabs per bank; arrows allow arbitrarily many categories.
            const size_t bank=(tab_/SettingsTemplate::tabs_per_bank)*SettingsTemplate::tabs_per_bank,end=std::min(pages.size(),bank+SettingsTemplate::tabs_per_bank);
            if(SettingsTemplate::show_tabs(pages))for(size_t i=bank;i<end;++i)button(SettingsTemplate::tab_x(i-bank),SettingsTemplate::tab_top,SettingsTemplate::tab_width,SettingsTemplate::control_height,pages[i].label,
                SAN14_CONTROL_TAB|(i==tab_?SAN14_CONTROL_SELECTED:0),Command::Tab,i,0,u8"切换设置分类。");
            if(bank)button(230,106,110,40,u8"上一组",0,Command::Tab,bank-1,0,u8"显示前面的分类。");
            if(end<pages.size())button(1580,106,110,40,u8"下一组",0,Command::Tab,end,0,u8"显示后面的分类。");
            const auto count=pages[tab_].rows.size();
            if(first_>=count)first_=0;
            if(first_)button(1450,980,140,40,u8"上一页",0,Command::Page,first_-SAN14_EXPERT_ROWS,0,"");
            if(first_+SAN14_EXPERT_ROWS<count)button(1610,980,140,40,u8"下一页",0,Command::Page,first_+SAN14_EXPERT_ROWS,0,"");
            if(count>SAN14_EXPERT_ROWS)label(1400,708,160,28,std::to_string(first_/SAN14_EXPERT_ROWS+1)+" / "+std::to_string((count+SAN14_EXPERT_ROWS-1)/SAN14_EXPERT_ROWS));
            for(size_t slot=0;slot<SAN14_EXPERT_ROWS&&first_+slot<count;++slot){const auto index=pages[tab_].rows[first_+slot];auto& r=rows_[index];
                auto& view=row_views[index];current_view=&view;
                const int y=SAN14_EXPERT_TOP+int(slot)*SAN14_EXPERT_ROW_PITCH;
                auto plate=label(SAN14_EXPERT_LEFT,y,SAN14_EXPERT_ROW_WIDTH,SAN14_EXPERT_ROW_HEIGHT,"",SAN14_CONTROL_EXPERT_ROW);
                check(host_->ui_style->bind_help(host_->host_context,plate,help,sv(r.help)));
                auto name=label(SAN14_EXPERT_LEFT+22,y+12,660,32,r.label,SAN14_CONTROL_EXPERT_LABEL);
                check(host_->ui_style->bind_help(host_->host_context,name,help,sv(r.help)));
                view.help_sources={plate,name};
                if(r.kind==Kind::Unavailable){
                    auto status=label(1160,y+12,280,32,u8"暂不可用");
                    check(host_->ui_style->bind_help(host_->host_context,status,help,sv(r.help)));
                    if(host_->ui_style->struct_size>=offsetof(San14UiStyleApi,set_tint)+sizeof(host_->ui_style->set_tint)&&
                       (host_->ui_style->capabilities&SAN14_UI_STYLE_TINT)&&host_->ui_style->set_tint){
                        check(host_->ui_style->set_tint(host_->host_context,name,0xff888888));
                        check(host_->ui_style->set_tint(host_->host_context,status,0xff888888));
                    }
                }else if(r.kind==Kind::Boolean){
                    auto yes=button(SettingsTemplate::choice_left,y+6,SettingsTemplate::choice_width,40,u8"有",SAN14_CONTROL_CHOICE_LEFT|(r.value?SAN14_CONTROL_SELECTED:0),Command::Change,index,1,r.help);
                    auto no=button(SettingsTemplate::choice_left+SettingsTemplate::choice_width,y+6,SettingsTemplate::choice_width,40,u8"无",SAN14_CONTROL_CHOICE_RIGHT|(!r.value?SAN14_CONTROL_SELECTED:0),Command::Change,index,0,r.help);
                    choices[index]={yes,no};
                }else if(r.kind==Kind::Slider){
                    if(has_slider_api(host_)){
                        auto key="slider."+std::to_string(++id);
                        San14SliderSpec ss{sizeof(ss),sv(key),SettingsTemplate::slider_left,y+6,SettingsTemplate::slider_width,SettingsTemplate::control_height,r.minimum,r.maximum,r.step,r.value,r.divisor,0,sv(r.suffix)};
                        San14ControlHandle slider_handle=0;
                        auto binding=std::make_unique<Binding>(Binding{this,Command::Change,index,r.value});
                        auto pointer=binding.get();bindings.push_back(std::move(binding));
                        check(host_->ui_slider->add_slider(host_->host_context,next,&ss,slider_changed,pointer,&slider_handle));
                        check(host_->ui_style->bind_help(host_->host_context,slider_handle,help,sv(r.help)));
                        view.help_sources.push_back(slider_handle);
                        const auto value_text=format_range_value(r.value,r.divisor,r.suffix);
                        const bool colored=range_changed_.count(r.key)&&has_tint_api()&&(host_->ui_style->capabilities&SAN14_UI_STYLE_BUTTON_TEXT_TINT);
                        auto readout=button(SettingsTemplate::slider_value_left,y+6,SettingsTemplate::slider_value_width,SettingsTemplate::control_height,value_text,0,Command::Edit,index,r.value,r.help,"range.edit."+r.key);
                        if(colored){
                            view.tinted_value=readout;view.button_text=true;
                        }
                        button(SettingsTemplate::slider_reset_left,y+6,SettingsTemplate::slider_reset_width,SettingsTemplate::control_height,u8"标准",0,Command::Change,index,r.standard,r.help);view.standard=bindings.back().get();
                        slider_views[index]={slider_handle,readout};
                    }else{
                        const int32_t prev=snap_range_value(r.value-r.step,r.minimum,r.maximum,r.step);
                        const int32_t inc=snap_range_value(r.value+r.step,r.minimum,r.maximum,r.step);
                        button(1060,y+6,64,40,"−",0,Command::Change,index,prev,r.help);
                        view.decrease=bindings.back().get();
                        view.readout=label(1134,y+12,110,32,format_range_value(r.value,r.divisor,r.suffix));
                        button(1250,y+6,64,40,"+",0,Command::Change,index,inc,r.help);
                        view.increase=bindings.back().get();
                        button(1350,y+6,144,40,u8"标准",0,Command::Change,index,r.standard,r.help);
                        view.standard=bindings.back().get();
                    }
                }else if(r.kind==Kind::Integer){
                    button(1060,y+6,64,40,"−",0,Command::Change,index,r.value>r.minimum?r.value-1:r.value,r.help);
                    view.decrease=bindings.back().get();
                    view.readout=label(1148,y+12,96,32,std::to_string(r.value));
                    button(1250,y+6,64,40,"+",0,Command::Change,index,r.value<r.maximum?r.value+1:r.value,r.help);
                    view.increase=bindings.back().get();
                    button(1350,y+6,144,40,u8"标准",0,Command::Change,index,r.standard,r.help);
                    view.standard=bindings.back().get();
                }else view.action=button(1160,y+6,280,40,action_captions_[r.key],0,Command::Change,index,0,r.help);
                if(view.readout&&range_changed_.count(r.key)&&has_tint_api())view.tinted_value=view.readout;
            }
            current_view=nullptr;
            const auto footer_label=label(360,918,1360,30,footer_actions_.empty()?u8"设置即时生效 · Tab / 方向键选择 · Enter 执行 · Esc 返回":footer_message_);
            const auto footer_hint_label=!footer_hint_.empty()?label(360,960,520,60,footer_hint_):San14ControlHandle{0};
            std::vector<San14ControlHandle> footer_controls;
            for(size_t i=0;i<footer_actions_.size();++i)
                footer_controls.push_back(button(1170+int(i)*290,1024,270,42,footer_actions_[i].first,0,Command::Footer,i,0,u8"提交待保存的设置。"));
            button(904,SAN14_EXPERT_FOOTER_TOP,112,112,u8"返回",SAN14_CONTROL_CONFIRM,Command::Close,0,0,u8"返回上一页。");
            check(host_->ui.show_panel(host_->host_context,next));
            auto old=panel_;panel_=next;help_=help;
            if(old)host_->ui.close_panel(host_->host_context,old);
            bindings_=std::move(bindings);choices_=std::move(choices);slider_views_=std::move(slider_views);row_views_=std::move(row_views);
            footer_label_=footer_label;footer_hint_label_=footer_hint_label;footer_controls_=std::move(footer_controls);
            for(const auto& entry:row_views_)tint_range(entry.first);
            if(!footer_actions_.empty())footer_status(footer_message_,footer_pending_);return SAN14_OK;
        }catch(San14Result result){if(next)host_->ui.close_panel(host_->host_context,next);return result;}
        catch(...){if(next)host_->ui.close_panel(host_->host_context,next);return SAN14_E_PLUGIN_FAILED;}
    }
};
}
#endif
#endif
