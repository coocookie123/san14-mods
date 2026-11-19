#pragma once
#include "san14_roster.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace save_stats {
constexpr uint32_t max_officers=6000;
constexpr size_t max_payload=1024*1024;
using Stats=std::array<int32_t,5>;
using Growth=std::array<int32_t,6>;
struct Officer {uint32_t id=0;std::string name,courtesy;Stats stats{};};
using Roster=std::map<uint32_t,Officer>;
template<size_t N> inline std::string fixed(const char (&text)[N]){
    const auto end=static_cast<const char*>(std::memchr(text,0,N));
    if(!end)throw std::runtime_error("Unterminated officer identity");
    return {text,static_cast<size_t>(end-text)};
}
inline Roster roster(const std::vector<San14RosterOfficer>& source){
    if(source.empty()||source.size()>max_officers)throw std::runtime_error("Invalid roster size");
    Roster out;
    for(const auto& row:source){
        if(row.struct_size<SAN14_ROSTER_OFFICER_V1_MIN_SIZE||
           (row.available_fields&(SAN14_ROSTER_NAMES|SAN14_ROSTER_BASE_STATS))!=(SAN14_ROSTER_NAMES|SAN14_ROSTER_BASE_STATS)||
           !row.officer_id||row.officer_id>max_officers)throw std::runtime_error("Incomplete officer baseline");
        Officer o;o.id=row.officer_id;o.name=fixed(row.name_utf8);o.courtesy=fixed(row.courtesy_utf8);
        if(o.name.empty())throw std::runtime_error("Empty officer identity");
        for(size_t i=0;i<5;++i){if(row.base_stats[i]<0||row.base_stats[i]>255)throw std::runtime_error("Invalid base ability");o.stats[i]=row.base_stats[i];}
        if(!out.emplace(o.id,std::move(o)).second)throw std::runtime_error("Duplicate officer identity");
    }
    return out;
}
inline std::vector<unsigned char> encode(const Roster& baseline){
    if(baseline.empty()||baseline.size()>max_officers)throw std::runtime_error("Invalid baseline count");
    std::vector<unsigned char> out={'S','G','S','1'};
    const auto append=[&](uint32_t value,unsigned n){for(unsigned i=0;i<n;++i)out.push_back(static_cast<unsigned char>(value>>(i*8)));};
    append(static_cast<uint32_t>(baseline.size()),4);
    for(const auto& entry:baseline){const auto& o=entry.second;
        if(!o.id||o.id>max_officers||entry.first!=o.id||o.name.empty()||o.name.size()>=128||o.courtesy.size()>=64||o.name.find('\0')!=std::string::npos||o.courtesy.find('\0')!=std::string::npos)throw std::runtime_error("Invalid baseline identity");
        append(o.id,2);for(auto stat:o.stats){if(stat<0||stat>255)throw std::runtime_error("Invalid baseline ability");append(static_cast<uint32_t>(stat),1);}
        append(static_cast<uint32_t>(o.name.size()),1);append(static_cast<uint32_t>(o.courtesy.size()),1);
        out.insert(out.end(),o.name.begin(),o.name.end());out.insert(out.end(),o.courtesy.begin(),o.courtesy.end());
        if(out.size()>max_payload)throw std::runtime_error("Baseline exceeds storage limit");
    }
    return out;
}
inline Roster decode(const std::vector<unsigned char>& bytes){
    if(bytes.size()<8||bytes.size()>max_payload||std::memcmp(bytes.data(),"SGS1",4))throw std::runtime_error("Unknown baseline format");
    size_t offset=4;
    const auto take=[&](unsigned n){if(n>bytes.size()-offset)throw std::runtime_error("Truncated baseline");uint32_t v=0;for(unsigned i=0;i<n;++i)v|=uint32_t(bytes[offset++])<<(i*8);return v;};
    const auto text=[&](uint32_t n){if(n>bytes.size()-offset)throw std::runtime_error("Truncated baseline identity");std::string s(reinterpret_cast<const char*>(bytes.data()+offset),n);offset+=n;if(s.find('\0')!=std::string::npos)throw std::runtime_error("Invalid baseline identity");return s;};
    const auto count=take(4);if(!count||count>max_officers)throw std::runtime_error("Invalid baseline count");
    Roster out;uint32_t previous=0;
    for(uint32_t i=0;i<count;++i){Officer o;o.id=take(2);if(o.id<=previous||o.id>max_officers)throw std::runtime_error("Invalid baseline ordering");previous=o.id;
        for(auto& stat:o.stats)stat=static_cast<int32_t>(take(1));
        const auto name_size=take(1),courtesy_size=take(1);if(!name_size||name_size>=128||courtesy_size>=64)throw std::runtime_error("Invalid baseline identity size");
        o.name=text(name_size);o.courtesy=text(courtesy_size);out.emplace(o.id,std::move(o));
    }
    if(offset!=bytes.size())throw std::runtime_error("Unexpected baseline tail");return out;
}
inline std::map<uint32_t,Growth> growth(const Roster& baseline,const Roster& current){
    std::map<uint32_t,Growth> out;
    for(const auto& entry:current){const auto found=baseline.find(entry.first);if(found==baseline.end())continue;
        const auto& before=found->second;const auto& now=entry.second;
        if(before.name!=now.name||before.courtesy!=now.courtesy)continue;
        Growth delta{};for(size_t i=0;i<5;++i){delta[i+1]=now.stats[i]-before.stats[i];delta[0]+=delta[i+1];}out.emplace(entry.first,delta);
    }
    return out;
}
}
