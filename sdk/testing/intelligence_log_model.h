#pragma once
#include <string>
#include <algorithm>
#include <utility>
#include <vector>
#include <cstdint>
namespace intelligence_log {
struct Row {
    uint32_t year=0,month=0,day=0,color=UINT32_MAX;
    std::wstring banner,text;
};
inline bool same_turn(const Row& a,const Row& b){return a.year==b.year&&a.month==b.month&&(a.day-1)/10==(b.day-1)/10;}
// Break long messages into native rows without losing semantic colors or
// splitting a UTF-16 surrogate pair. The renderer supplies a conservative
// width derived from the stock body width and row height, not guessed pixels.
inline std::vector<Row> wrap(const std::vector<Row>& rows,unsigned half_cells){
    half_cells=(std::max)(2u,half_cells);std::vector<Row> out;
    for(const auto& input:rows){
        Row line=input;line.text.clear();std::wstring color=L"^00";unsigned used=0;
        for(size_t i=0;i<input.text.size();){
            if(input.text[i]==L'^'&&i+2<input.text.size()){
                color=input.text.substr(i,3);line.text+=color;i+=3;continue;
            }
            const auto c=input.text[i];const size_t n=c>=0xd800&&c<=0xdbff&&i+1<input.text.size()&&input.text[i+1]>=0xdc00&&input.text[i+1]<=0xdfff?2:1;
            const unsigned width=c<128?1:2;
            if(used&&used+width>half_cells){
                line.text+=L"^00";out.push_back(std::move(line));line=input;
                line.color=UINT32_MAX;line.banner.clear();line.text=color;used=0;
            }
            line.text.append(input.text,i,n);used+=width;i+=n;
        }
        line.text+=L"^00";out.push_back(std::move(line));
    }
    return out;
}
inline bool valid(const Row& r){
    if(r.year<150||r.year>400||r.month<1||r.month>12||r.day<1||r.day>30||
       (r.color!=UINT32_MAX&&r.color>61)||r.banner.size()>2||r.text.size()>2048)return false;
    for(size_t i=0;i<r.text.size();++i){
        if(r.text[i]<32)return false;
        if(r.text[i]==L'^'){
            if(i+2>=r.text.size()||r.text[i+1]!=L'0'||(r.text[i+2]!=L'0'&&r.text[i+2]!=L'2'&&r.text[i+2]!=L'5'))return false;
            i+=2;
        }
    }
    for(auto c:r.banner)if(c<32||c==L'^')return false;
    return true;
}
}
