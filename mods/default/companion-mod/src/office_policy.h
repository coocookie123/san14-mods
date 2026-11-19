#pragma once
// Companion policy over copied values. No game address, native structure,
// hook implementation or Host state belongs in this file.
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <numeric>
#include <vector>

namespace companion_office_policy {
enum Mode : uint32_t {Comprehensive=0,Military=1,Civil=2};
struct Officer {
    uint32_t id=0;
    int32_t leadership=0,martial=0,intelligence=0,politics=0,merit=0;
};
inline bool valid(const Officer& officer){return officer.id>=1&&officer.id<=6000;}
inline int32_t stat(int32_t value){return std::clamp(value,0,255);}
inline int32_t positive_merit(const Officer& officer){return std::clamp(officer.merit,0,32767);}
// -1 means unknown: preserve the native candidate decision.
inline int32_t military_class(const Officer& officer) {
    if(!valid(officer))return -1;
    return stat(officer.leadership)+stat(officer.martial)>140||
        stat(officer.leadership)+stat(officer.intelligence)>160?1:0;
}
inline int64_t score(const Officer& officer,int32_t maximum_merit,Mode mode) {
    if(!valid(officer))return -1;
    const int64_t lead=stat(officer.leadership),intel=stat(officer.intelligence),politics=stat(officer.politics);
    const int64_t merit=positive_merit(officer),maximum=std::max(maximum_merit,1);
    const int64_t military=(lead*30+intel*15)*maximum+merit*5500;
    const int64_t civil=(politics*30+intel*10)*maximum+merit*6000;
    return mode==Military?military:mode==Civil?civil:std::max(military,civil);
}
inline int32_t maximum_merit(const std::vector<Officer>& officers) {
    int32_t maximum=1;
    for(const auto& officer:officers)if(valid(officer))maximum=std::max(maximum,positive_merit(officer));
    return maximum;
}
// Returned indexes are a stable permutation of the original candidate set.
inline std::vector<size_t> rank(const std::vector<Officer>& officers,Mode mode) {
    std::vector<size_t> result(officers.size());std::iota(result.begin(),result.end(),size_t(0));
    const auto maximum=maximum_merit(officers);
    std::stable_sort(result.begin(),result.end(),[&](size_t left,size_t right){
        const auto a=score(officers[left],maximum,mode),b=score(officers[right],maximum,mode);
        if(a!=b)return a>b;
        const auto aid=valid(officers[left])?officers[left].id:UINT32_C(0xffff);
        const auto bid=valid(officers[right])?officers[right].id:UINT32_C(0xffff);
        return aid<bid;
    });
    return result;
}
inline int32_t classify_post(uint32_t post_id,uint32_t stat_index,const int32_t (&bonuses)[4]) {
    if(stat_index>3)return 0;
    if(post_id<1||post_id>72)return bonuses[stat_index];
    for(const auto bonus:bonuses)if(bonus>0)return bonuses[stat_index];
    const uint32_t mask=post_id==1||post_id==5?0xf:((post_id-1)&4)==0?0xc:0x3;
    return static_cast<int32_t>((mask>>stat_index)&1);
}
}
