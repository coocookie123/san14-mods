#pragma once
#include "common.h"
#include "archive_extract.h"
#include <map>
#include <set>
// ZIP admission before invoking the OS extractor. No scripts or plugin code run.
namespace mod_packages {
struct File {std::string name;uint32_t size;bool directory;};
inline std::wstring identity(std::wstring s){for(auto& c:s)c=(wchar_t)towlower(c);return s;}
inline void plain_path(const fs::path& path){
    auto absolute=fs::absolute(path);fs::path part;
    for(const auto& component:absolute){part/=component;auto flags=GetFileAttributesW(part.c_str());
        if(flags!=INVALID_FILE_ATTRIBUTES&&(flags&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Linked package path refused");}
}
inline std::string safe_name(std::string name){
    if(name.empty()||name.size()>220||name.front()=='/'||name.find_first_of("\\:\0",0,3)!=std::string::npos)throw std::runtime_error("Unsafe ZIP path");
    if(name.back()=='/')name.pop_back();if(name.empty()||name.back()=='/')throw std::runtime_error("Empty ZIP path");
    wide(name);std::istringstream stream(name);std::string part;
    while(std::getline(stream,part,'/')){
        if(part.empty()||part=="."||part==".."||part.back()=='.'||part.back()==' '||part.find_first_of("<>\"|?*")!=std::string::npos)throw std::runtime_error("Unsafe ZIP path component");
        for(unsigned char c:part)if(c<32)throw std::runtime_error("Control character in ZIP path");
        auto stem=lower(part.substr(0,part.find('.')));
        if(stem=="con"||stem=="prn"||stem=="aux"||stem=="nul"||stem=="conin$"||stem=="conout$"||(stem.size()==4&&(stem.substr(0,3)=="com"||stem.substr(0,3)=="lpt")&&stem[3]>='0'&&stem[3]<='9'))throw std::runtime_error("Reserved ZIP path");
    }return name;
}
template<class T>T field(const std::vector<unsigned char>& bytes,size_t at){if(at>bytes.size()||sizeof(T)>bytes.size()-at)throw std::runtime_error("Truncated ZIP");T value{};memcpy(&value,bytes.data()+at,sizeof(value));return value;}
inline void extra_fields(const std::vector<unsigned char>& bytes,size_t at,size_t size){
    const auto end=at+size;if(end>bytes.size())throw std::runtime_error("Truncated ZIP metadata");
    while(at<end){if(end-at<4)throw std::runtime_error("Invalid ZIP metadata");auto type=field<uint16_t>(bytes,at),length=field<uint16_t>(bytes,at+2);
        // Admit timestamps only; reject Unicode path overrides, links, ZIP64,
        // and vendor records that an extractor could interpret differently.
        if((type!=0x5455&&type!=0x000a)||size_t(length)>end-at-4)throw std::runtime_error("Unsupported ZIP metadata; use a standard UTF-8 ZIP");at+=4+length;}
}
inline std::vector<File> inspect(const std::vector<unsigned char>& b){
    if(b.size()<22||b.size()>128*1024*1024)throw std::runtime_error("ZIP must be at most 128 MiB");
    size_t end=b.size();for(size_t p=b.size()-22;;--p){if(field<uint32_t>(b,p)==0x06054b50&&p+22+field<uint16_t>(b,p+20)==b.size()){end=p;break;}if(p==0||b.size()-p>65557)break;}
    if(end==b.size()||field<uint16_t>(b,end+4)||field<uint16_t>(b,end+6))throw std::runtime_error("Unsupported ZIP volume");
    auto count=field<uint16_t>(b,end+10);size_t at=field<uint32_t>(b,end+16),central=at,central_size=field<uint32_t>(b,end+12);
    if(!count||count>2048||count!=field<uint16_t>(b,end+8)||at>end||central_size!=end-at)throw std::runtime_error("Unsupported ZIP directory");
    uint64_t total=0;std::vector<File> files;std::set<std::wstring> names;std::vector<std::pair<size_t,size_t>> spans;
    for(unsigned i=0;i<count;++i){
        if(field<uint32_t>(b,at)!=0x02014b50)throw std::runtime_error("Invalid ZIP entry");
        auto flags=field<uint16_t>(b,at+8),method=field<uint16_t>(b,at+10),n=field<uint16_t>(b,at+28),extra=field<uint16_t>(b,at+30),comment=field<uint16_t>(b,at+32);
        auto packed=field<uint32_t>(b,at+20),size=field<uint32_t>(b,at+24),offset=field<uint32_t>(b,at+42),attrs=field<uint32_t>(b,at+38);
        if(flags&~0x80eu||(method!=0&&method!=8)||field<uint16_t>(b,at+34)||at+46+size_t(n)+extra+comment>end)throw std::runtime_error("Encrypted/unsupported ZIP entry");
        const auto kind=(attrs>>16)&0170000;if(kind&&kind!=0100000&&kind!=0040000)throw std::runtime_error("ZIP link/device refused");
        if(attrs&FILE_ATTRIBUTE_REPARSE_POINT)throw std::runtime_error("ZIP reparse point refused");
        std::string raw((const char*)b.data()+at+46,n);bool directory=!raw.empty()&&raw.back()=='/';auto name=safe_name(raw);
        extra_fields(b,at+46+n,extra);
        if(!names.insert(identity(wide(name))).second)throw std::runtime_error("Duplicate ZIP path");
        if(size>64*1024*1024||(total+=size)>256*1024*1024||(directory&&size))throw std::runtime_error("ZIP expansion limit");
        if(field<uint32_t>(b,offset)!=0x04034b50||field<uint16_t>(b,offset+6)!=flags||field<uint16_t>(b,offset+8)!=method||field<uint16_t>(b,offset+26)!=n)throw std::runtime_error("ZIP headers disagree");
        size_t start=size_t(offset)+30,end_data=start+n+field<uint16_t>(b,offset+28)+packed;
        extra_fields(b,start+n,field<uint16_t>(b,offset+28));
        if(end_data>central||start+n>b.size()||memcmp(b.data()+start,raw.data(),n))throw std::runtime_error("ZIP local path/range mismatch");
        spans.push_back({offset,end_data});files.push_back({name,size,directory});at+=46+size_t(n)+extra+comment;
    }
    if(at!=end)throw std::runtime_error("ZIP directory size mismatch");
    std::sort(spans.begin(),spans.end());if(spans.front().first)throw std::runtime_error("Self-extracting/prefixed ZIP refused");for(size_t i=1;i<spans.size();++i)if(spans[i].first<spans[i-1].second)throw std::runtime_error("Overlapping ZIP entries");
    return files;
}
inline fs::path extract(const fs::path& archive,const fs::path& staging){
    if(lower(utf8(archive.extension().wstring()))!=".s14mod")throw std::runtime_error("Select a .s14mod package");
    plain_path(staging);if(fs::exists(staging))throw std::runtime_error("Staging directory already exists");
    auto bytes=read_file(archive,128*1024*1024);auto files=inspect(bytes);fs::create_directories(staging);
    // Extract the inspected bytes, not a source file that could change afterwards.
    const auto copy=staging/L"input.zip",destination=staging/L"content";write_atomic(copy,bytes.data(),bytes.size());fs::create_directory(destination);
    archive_extract::extract_zip(copy,destination,staging,"ZIP extraction failed",30000);
    std::map<std::wstring,uint32_t> expected;for(const auto& file:files)if(!file.directory)expected.emplace(identity(wide(file.name)),file.size);
    fs::path manifest;unsigned manifests=0;
    for(const auto& item:fs::recursive_directory_iterator(destination)){
        plain_path(item.path());if(!item.is_regular_file())continue;auto relative=fs::relative(item.path(),destination).generic_wstring();auto found=expected.find(identity(relative));
        if(found==expected.end()||item.file_size()!=found->second)throw std::runtime_error("Extracted inventory mismatch");expected.erase(found);
        if(item.path().filename()==L"mod.json"){manifest=item.path();++manifests;}
    }
    if(!expected.empty()||manifests!=1)throw std::runtime_error("Select a .s14mod package containing exactly one mod.json");
    auto root=manifest.parent_path();for(const auto& file:files)if(!file.directory&&!inside(root,destination/wide(file.name)))throw std::runtime_error("Files outside Mod root");return root;
}
}
