#pragma once
#include "san14_host.h"
#include "san14_officer_traits.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace enhanced_settings_mod {
class OfficerTraitsDialog {
public:
    static bool supported(const San14HostApi* h){
        return h&&h->struct_size>=offsetof(San14HostApi,officer_traits)+sizeof(h->officer_traits)&&h->officer_traits&&
            h->officer_traits->struct_size>=sizeof(San14OfficerTraitsApi)&&h->officer_traits->query&&h->officer_traits->set_expanded&&
            h->officer_traits->read&&h->officer_traits->options&&h->officer_traits->replace_extra&&
            h->roster.struct_size>=sizeof(San14RosterApi)&&h->roster.capture&&
            h->ui_extended.struct_size>=offsetof(San14UiExtendedApi,add_text_input)+sizeof(h->ui_extended.add_text_input)&&h->ui_extended.add_text_input;
    }
    void bind(const San14HostApi* h){host_=h;}
    void reset(){close();officers_.clear();options_.clear();names_.clear();selected_=0;undo_valid_=false;search_.clear();}
    San14Result open(){
        if(!supported(host_))return SAN14_E_UNSUPPORTED;
        San14OfficerTraitsStatus status{sizeof(status)};
        auto r=host_->officer_traits->query(host_->host_context,&status);if(r!=SAN14_OK)return r;
        if(!(status.flags&SAN14_TRAITS_CAN_EDIT)||status.active_slots!=9)return SAN14_E_BUSY;
        r=capture_officers();if(r!=SAN14_OK)return r;
        r=capture_options();if(r!=SAN14_OK)return r;
        close();selected_=0;undo_valid_=false;search_.clear();first_=0;mode_=Mode::Officers;status_.clear();return render();
    }
private:
    enum class Mode{Officers,Main,Options};
    enum class Action{Officer,Pick,Option,Search,Previous,Next,Apply,Undo,ChooseOfficer,Back,Close};
    struct Binding{OfficerTraitsDialog* self;Action action;uint32_t value;};
    struct Officer{uint32_t id;std::string name;};
    const San14HostApi* host_=nullptr;
    San14PanelHandle panel_=0;
    San14ControlHandle status_label_=0;
    std::vector<std::unique_ptr<Binding>> bindings_;
    std::vector<Officer> officers_;
    std::vector<San14OfficerTraitOption> options_;
    std::map<uint32_t,std::string> names_;
    San14OfficerTraits loaded_{};
    std::array<uint16_t,4> pending_{},undo_{};
    bool undo_valid_=false;
    uint32_t selected_=0,slot_=0;
    Mode mode_=Mode::Officers;
    size_t first_=0;
    std::string search_,status_;
    static constexpr size_t kRows=8;
    static San14StringView sv(const std::string& s){return {s.data(),static_cast<uint32_t>(s.size())};}
    template<size_t N>static std::string bounded(const char(&s)[N]){
        const auto end=static_cast<const char*>(std::memchr(s,0,N));return end?std::string(s,end):std::string{};
    }
    std::string trait_name(uint16_t id)const{
        if(!id)return u8"无";
        const auto found=names_.find(id);return found!=names_.end()?found->second:(u8"未知个性 #"+std::to_string(id));
    }
    std::string officer_name()const{
        const auto found=std::find_if(officers_.begin(),officers_.end(),[&](const auto& o){return o.id==selected_;});
        return found==officers_.end()?std::to_string(selected_):found->name;
    }
    San14Result capture_officers(){
        San14RosterInfo info{sizeof(info)};uint32_t count=0;
        auto r=host_->roster.capture(host_->host_context,&info,nullptr,sizeof(San14RosterOfficer),0,&count);
        if(r!=SAN14_OK&&r!=SAN14_E_BUFFER_TOO_SMALL)return r;
        if(count>6000)return SAN14_E_INVALID_ARGUMENT;
        std::vector<San14RosterOfficer> records(count);
        for(auto& row:records)row.struct_size=sizeof(row);
        info.struct_size=sizeof(info);uint32_t actual=0;
        r=host_->roster.capture(host_->host_context,&info,records.data(),sizeof(San14RosterOfficer),count,&actual);
        if(r!=SAN14_OK)return r;if(actual>count)return SAN14_E_STALE_GENERATION;
        officers_.clear();
        for(uint32_t i=0;i<actual;++i){const auto& row=records[i];const auto name=bounded(row.name_utf8);
            if(row.officer_id>=1&&row.officer_id<=6000&&!name.empty())officers_.push_back({row.officer_id,name});}
        return officers_.empty()?SAN14_E_NOT_FOUND:SAN14_OK;
    }
    San14Result capture_options(){
        uint32_t count=0;auto r=host_->officer_traits->options(host_->host_context,nullptr,sizeof(San14OfficerTraitOption),0,&count);
        if(r!=SAN14_OK&&r!=SAN14_E_BUFFER_TOO_SMALL)return r;if(count>4096)return SAN14_E_INVALID_ARGUMENT;
        std::vector<San14OfficerTraitOption> records(count);uint32_t actual=0;
        r=host_->officer_traits->options(host_->host_context,records.data(),sizeof(San14OfficerTraitOption),count,&actual);
        if(r!=SAN14_OK)return r;if(actual>count)return SAN14_E_STALE_GENERATION;
        options_.clear();names_.clear();
        for(uint32_t i=0;i<actual;++i){const auto& row=records[i];const auto name=bounded(row.name_utf8);
            if(row.id&&row.id<=65535&&!name.empty()&&!names_.count(row.id)){options_.push_back(row);names_[row.id]=name;}}
        return SAN14_OK;
    }
    void close(){
        if(panel_&&host_&&host_->ui.close_panel)host_->ui.close_panel(host_->host_context,panel_);
        panel_=0;status_label_=0;bindings_.clear();
    }
    San14Result load_officer(uint32_t id){
        San14OfficerTraits state{sizeof(state)};const auto r=host_->officer_traits->read(host_->host_context,id,&state);
        if(r!=SAN14_OK)return r;if(state.officer_id!=id)return SAN14_E_STALE_GENERATION;
        if(state.active_slots!=9)return SAN14_E_BUSY;
        loaded_=state;selected_=id;for(size_t i=0;i<4;++i)pending_[i]=state.ids[i+5];
        return SAN14_OK;
    }
    void show_status(const std::string& text){status_=text;if(panel_&&status_label_)host_->ui.set_text(host_->host_context,status_label_,sv(status_));}
    void failure(San14Result r){
        if(r==SAN14_E_STALE_GENERATION)show_status(u8"战局已切换或数据已变化，请返回后重新选择武将。");
        else if(r==SAN14_E_BUSY)show_status(u8"当前无法编辑。请先在主菜单开启9个个性；出阵武将需先归城，再重新选择。");
        else show_status(u8"未能应用（"+std::to_string(r)+u8"），原有个性保持不变。");
    }
    San14Result replace(const std::array<uint16_t,4>& values){
        San14OfficerTraitsStatus status{sizeof(status)};auto r=host_->officer_traits->query(host_->host_context,&status);
        if(r!=SAN14_OK)return r;if(status.generation!=loaded_.generation)return SAN14_E_STALE_GENERATION;
        if(!(status.flags&SAN14_TRAITS_CAN_EDIT)||status.active_slots!=9)return SAN14_E_BUSY;
        San14OfficerExtraTraitsEdit edit{sizeof(edit),selected_,loaded_.generation,{}};
        std::copy(values.begin(),values.end(),edit.ids);return host_->officer_traits->replace_extra(host_->host_context,&edit);
    }
    void invoke(Action action,uint32_t value){
        switch(action){
        case Action::Close:close();return;
        case Action::Search:first_=0;render();return;
        case Action::Previous:first_=first_>=kRows?first_-kRows:0;render();return;
        case Action::Next:first_+=kRows;render();return;
        case Action::ChooseOfficer:mode_=Mode::Officers;search_.clear();first_=0;status_.clear();render();return;
        case Action::Officer:{
            const auto r=load_officer(value);if(r!=SAN14_OK){failure(r);return;}
            undo_valid_=false;mode_=Mode::Main;status_=(loaded_.flags&SAN14_TRAITS_DEPLOYED)?u8"该武将已出阵，请先归城，再重新选择后编辑。":u8"前五项仅供查看。修改额外个性后，请保存并重新载入，让游戏重新计算个性。";
            render();return;}
        case Action::Pick:if(loaded_.flags&SAN14_TRAITS_DEPLOYED){failure(SAN14_E_BUSY);return;}slot_=value;mode_=Mode::Options;search_.clear();first_=0;status_.clear();render();return;
        case Action::Option:pending_[slot_]=static_cast<uint16_t>(value);mode_=Mode::Main;status_=u8"选择尚未应用。点击“应用”写入第6–9项。";render();return;
        case Action::Back:if(!selected_){close();return;}mode_=Mode::Main;render();return;
        case Action::Apply:{
            if(loaded_.flags&SAN14_TRAITS_DEPLOYED){failure(SAN14_E_BUSY);return;}
            const auto before=loaded_;const auto r=replace(pending_);if(r!=SAN14_OK){failure(r);return;}
            for(size_t i=0;i<4;++i)undo_[i]=before.ids[i+5];undo_valid_=true;
            const auto refreshed=load_officer(selected_);if(refreshed!=SAN14_OK){failure(refreshed);return;}
            status_=u8"已应用第6–9项，原五项未改。请保存并重新载入，让游戏重新计算个性；重新出征后生效。";render();return;}
        case Action::Undo:{
            if(undo_valid_){const auto r=replace(undo_);if(r!=SAN14_OK){failure(r);return;}undo_valid_=false;
                const auto refreshed=load_officer(selected_);if(refreshed!=SAN14_OK){failure(refreshed);return;}status_=u8"已撤销上次应用。请保存并重新载入，让游戏重新计算个性。";
            }else{for(size_t i=0;i<4;++i)pending_[i]=loaded_.ids[i+5];status_=u8"已撤销未应用的选择。";}
            render();return;}
        }
    }
    static void SAN14_MOD_CALL click(void* ctx,San14ControlHandle){const auto b=*static_cast<Binding*>(ctx);try{b.self->invoke(b.action,b.value);}catch(...){b.self->failure(SAN14_E_PLUGIN_FAILED);}}
    static void SAN14_MOD_CALL search_changed(void* ctx,San14ControlHandle,San14StringView value){auto* self=static_cast<OfficerTraitsDialog*>(ctx);self->search_=value.data?std::string(value.data,value.length):std::string{};}
    San14Result render(){
        San14PanelHandle next=0;San14ControlHandle new_status=0;std::vector<std::unique_ptr<Binding>> bindings;
        try{
            const std::string title=mode_==Mode::Main?u8"武将额外个性":mode_==Mode::Officers?u8"选择武将":u8"选择第"+std::to_string(slot_+6)+u8"个个性";
            San14PanelSpec spec{sizeof(spec),sv(title),1120,820,0};auto r=host_->ui.create_panel(host_->host_context,&spec,&next);if(r!=SAN14_OK)return r;
            const auto label=[&](const std::string& id,const std::string& text,int x,int y,int w,int h,San14ControlHandle* output=nullptr){
                San14ControlSpec cs{sizeof(cs),sv(id),sv(text),x,y,w,h,0};San14ControlHandle control=0;const auto result=host_->ui.add_label(host_->host_context,next,&cs,&control);if(result!=SAN14_OK)throw result;if(output)*output=control;
            };
            const auto button=[&](const std::string& id,const std::string& text,int x,int y,int w,int h,Action action,uint32_t value=0){
                San14ControlSpec cs{sizeof(cs),sv(id),sv(text),x,y,w,h,0};auto binding=std::make_unique<Binding>(Binding{this,action,value});San14ControlHandle control=0;
                const auto result=host_->ui.add_button(host_->host_context,next,&cs,click,binding.get(),&control);if(result!=SAN14_OK)throw result;bindings.push_back(std::move(binding));
            };
            label("traits.status",status_,36,650,1048,76,&new_status);
            if(mode_==Mode::Main){
                label("traits.officer",u8"武将："+officer_name()+" #"+std::to_string(selected_),36,48,760,40);
                button("traits.choose",u8"选择武将",824,48,240,40,Action::ChooseOfficer);
                for(size_t i=0;i<9;++i){
                    const auto id=i<5?loaded_.ids[i]:pending_[i-5];const auto text=u8"个性"+std::to_string(i+1)+u8"："+trait_name(id)+(i<5?u8"（只读）":"");
                    if(i<5)label("traits.base."+std::to_string(i),text,52,106+int(i)*54,1012,42);
                    else button("traits.extra."+std::to_string(i-5),text+u8" ▼",52,106+int(i)*54,1012,42,Action::Pick,static_cast<uint32_t>(i-5));
                }
                button("traits.apply",u8"应用",240,740,180,44,Action::Apply);
                button("traits.undo",u8"撤销",470,740,180,44,Action::Undo);
                button("traits.close",u8"返回",700,740,180,44,Action::Close);
            }else{
                label("traits.search.label",mode_==Mode::Officers?u8"按姓名或编号搜索：":u8"按个性名称或编号搜索：",36,52,340,36);
                const std::string input_id="traits.search";
                San14ControlSpec input{sizeof(input),sv(input_id),sv(search_),376,48,460,40,0};San14ControlHandle ctrl=0;
                r=host_->ui_extended.add_text_input(host_->host_context,next,&input,128,search_changed,this,&ctrl);if(r!=SAN14_OK)throw r;
                button("traits.search.go",u8"搜索",864,48,200,40,Action::Search);
                std::vector<std::pair<uint32_t,std::string>> filtered;
                if(mode_==Mode::Officers){for(const auto& o:officers_)if(search_.empty()||o.name.find(search_)!=std::string::npos||std::to_string(o.id).find(search_)!=std::string::npos)filtered.push_back({o.id,o.name});}
                else{filtered.push_back({0,u8"无"});for(const auto& o:options_){const auto name=names_.at(o.id);if(search_.empty()||name.find(search_)!=std::string::npos||std::to_string(o.id).find(search_)!=std::string::npos)filtered.push_back({o.id,name});}}
                if(first_>=filtered.size())first_=0;
                for(size_t row=0;row<kRows&&first_+row<filtered.size();++row){const auto& item=filtered[first_+row];button("traits.item."+std::to_string(row),std::to_string(item.first)+" "+item.second,52,112+int(row)*60,1012,48,mode_==Mode::Officers?Action::Officer:Action::Option,item.first);}
                label("traits.pages",filtered.empty()?u8"没有匹配项":std::to_string(first_/kRows+1)+" / "+std::to_string((filtered.size()+kRows-1)/kRows),450,600,220,34);
                if(first_)button("traits.prev",u8"上一页",100,740,220,44,Action::Previous);
                if(first_+kRows<filtered.size())button("traits.next",u8"下一页",450,740,220,44,Action::Next);
                button("traits.back",u8"返回",800,740,220,44,Action::Back);
            }
            r=host_->ui.show_panel(host_->host_context,next);if(r!=SAN14_OK)throw r;
            const auto old=panel_;panel_=next;status_label_=new_status;bindings_=std::move(bindings);if(old)host_->ui.close_panel(host_->host_context,old);return SAN14_OK;
        }catch(San14Result r){if(next)host_->ui.close_panel(host_->host_context,next);return r;}
        catch(...){if(next)host_->ui.close_panel(host_->host_context,next);return SAN14_E_PLUGIN_FAILED;}
    }
};
}
