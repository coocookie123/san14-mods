#include "../src/save_stats_core.h"
#include <cassert>
#include <iostream>

template<class F> bool rejects(F f){try{f();return false;}catch(const std::exception&){return true;}}
int main(){
    using namespace save_stats;
    Officer a{1,u8"赵云",u8"子龙",{70,80,60,50,60}},b{6000,u8"新武将","",{1,100,0,90,90}};
    Roster start{{a.id,a},{b.id,b}};const auto bytes=encode(start);auto restored=decode(bytes);
    assert(restored.size()==2&&restored.at(1).stats==a.stats&&restored.at(6000).name==b.name);
    auto now=start;now.at(1).stats={76,78,60,53,59};now.at(6000).courtesy=u8"换人";
    auto delta=growth(restored,now);assert(delta.size()==1&&delta.at(1)==Growth({6,6,-2,0,3,-1}));
    now.emplace(42,Officer{42,u8"后来者","",{100,100,100,100,100}});assert(!growth(start,now).count(42));
    auto corrupt=bytes;corrupt[3]='2';assert(rejects([&]{decode(corrupt);}));
    for(size_t n=0;n<bytes.size();++n){auto short_bytes=bytes;short_bytes.resize(n);assert(rejects([&]{decode(short_bytes);}));}
    corrupt=bytes;corrupt.push_back(0);assert(rejects([&]{decode(corrupt);}));
    corrupt=bytes;corrupt[4]=0xff;corrupt[5]=0xff;assert(rejects([&]{decode(corrupt);}));
    corrupt=bytes;corrupt[8]=0;assert(rejects([&]{decode(corrupt);}));
    assert(rejects([&]{encode({});}));
    auto bad=start;bad.at(1).stats[0]=256;assert(rejects([&]{encode(bad);}));
    San14RosterOfficer row{};row.struct_size=sizeof(row);row.officer_id=1;strcpy_s(row.name_utf8,u8"赵云");strcpy_s(row.courtesy_utf8,u8"子龙");
    row.available_fields=SAN14_ROSTER_NAMES|SAN14_ROSTER_BASE_STATS;
    for(int i=0;i<5;++i)row.base_stats[i]=a.stats[i];
    assert(roster({row}).at(1).stats==a.stats);
    assert(rejects([&]{roster({row,row});}));row.available_fields=SAN14_ROSTER_NAMES;assert(rejects([&]{roster({row});}));
    std::cout<<"Save stats core: binary bounds, identity changes, signed growth, total and missing baselines passed\n";
}
