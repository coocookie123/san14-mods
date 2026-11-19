#ifndef SAN14_SETTINGS_TEMPLATE_H
#define SAN14_SETTINGS_TEMPLATE_H
#include "san14_ui_style.h"
#ifdef __cplusplus
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
namespace san14 {
// Shared stepped range snap and fixed-point formatting helpers.
inline int32_t snap_range_value(int32_t value,int32_t minimum,int32_t maximum,int32_t step){
    if(minimum>=maximum)return minimum;
    const int64_t span=int64_t(maximum)-int64_t(minimum);
    const int64_t safe_step=step>0?int64_t(step):1;
    int64_t offset=int64_t(value)-int64_t(minimum);
    if(offset<=0)return minimum;
    if(offset>=span)return maximum;
    int64_t steps=(offset+safe_step/2)/safe_step;
    int64_t snapped=int64_t(minimum)+steps*safe_step;
    if(snapped>maximum)snapped=maximum;
    return static_cast<int32_t>(snapped);
}
inline std::string format_range_value(int32_t value,int32_t divisor,const std::string& suffix){
    std::string out;
    if(divisor<=1){
        out=std::to_string(value);
    }else{
        int64_t v=value;
        if(v<0){out.push_back('-');v=-v;}
        const int64_t whole=v/divisor,rem=v%divisor;
        int decimals=0;
        for(int64_t d=divisor;d>1&&decimals<6;d/=10)++decimals;
        if(decimals<=0)decimals=1;
        out+=std::to_string(whole)+'.';
        std::string frac=std::to_string(rem);
        if(int(frac.size())<decimals)out.append(size_t(decimals)-frac.size(),'0');
        out+=frac;
    }
    if(!suffix.empty())out+=suffix;
    return out;
}
inline bool parse_range_input(const std::string& raw,int32_t minimum,int32_t maximum,int32_t step,int32_t divisor,int32_t& out){
    size_t begin=0,end=raw.size();
    while(begin<end&&static_cast<unsigned char>(raw[begin])<=0x20)++begin;
    while(end>begin){
        const char ch=raw[end-1];
        if(static_cast<unsigned char>(ch)<=0x20||ch=='x'||ch=='X'||ch=='%')--end;
        else break;
    }
    if(begin>=end)return false;
    bool neg=false;
    if(raw[begin]=='-'||raw[begin]=='+'){neg=(raw[begin]=='-');++begin;}
    if(begin>=end)return false;
    bool seen_dot=false;size_t digits=0;double val=0.0,frac=0.1;
    for(size_t i=begin;i<end;++i){
        const char ch=raw[i];
        if(ch>='0'&&ch<='9'){
            ++digits;
            if(!seen_dot)val=val*10.0+(ch-'0');
            else{val+=(ch-'0')*frac;frac*=0.1;}
            if(!std::isfinite(val)||val>1e12)return false;
        }else if(ch=='.'&&!seen_dot){
            seen_dot=true;
        }else return false;
    }
    if(!digits)return false;
    if(neg)val=-val;
    const double scaled=val*static_cast<double>(divisor>0?divisor:1);
    if(!std::isfinite(scaled)||scaled<double(std::numeric_limits<int64_t>::min()/2)||scaled>double(std::numeric_limits<int64_t>::max()/2))return false;
    const int64_t rounded=std::llround(scaled);
    const int64_t clamped=std::clamp<int64_t>(rounded,minimum,maximum);
    out=snap_range_value(static_cast<int32_t>(clamped),minimum,maximum,step);
    return true;
}
// Shared default template for registered settings and custom ExpertPage pages.
// Categories retain first occurrence order; row indices retain registration order.
struct SettingsCategory {std::string label;std::vector<size_t> rows;bool explicit_label=false;};
template<class Rows> std::vector<SettingsCategory> settings_categories(const Rows& rows){
    std::vector<SettingsCategory> out;
    for(size_t i=0;i<rows.size();++i){
        const std::string label=rows[i].category.empty()?u8"常规":rows[i].category;
        auto found=std::find_if(out.begin(),out.end(),[&](const auto& c){return c.label==label;});
        if(found==out.end()){out.push_back({label,{}});found=out.end()-1;}
        found->explicit_label=found->explicit_label||!rows[i].category.empty();
        found->rows.push_back(i);
    }
    if(out.empty())out.push_back({u8"常规",{}});
    return out;
}
struct SettingsTemplate {
    static constexpr size_t tabs_per_bank=6;
    static constexpr int tab_top=106,tab_width=200,control_height=40;
    static constexpr int choice_left=1080,choice_width=180;
    static constexpr int slider_left=900,slider_width=364;
    static constexpr int slider_value_left=1280,slider_value_width=124;
    static constexpr int slider_reset_left=1418,slider_reset_width=124;
    static bool show_tabs(size_t categories){return categories>1;}
    static bool show_tabs(const std::vector<SettingsCategory>& categories){
        return categories.size()>1||(!categories.empty()&&categories.front().explicit_label);
    }
    static int tab_x(size_t slot){return SAN14_EXPERT_LEFT+int(slot)*tab_width;}
};
}
#endif
#endif
