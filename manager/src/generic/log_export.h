#pragma once

#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>

namespace log_export {
namespace fs = std::filesystem;

struct Result { uint64_t inspected = 0; uint64_t exported = 0; };

inline std::string source_of(const std::string& line,const std::set<std::string>& known_mods) {
    const auto marker=line.find(" source=");
    if(marker!=std::string::npos){
        const auto first=marker+8,end=line.find(' ',first);
        if(end!=first)return line.substr(first,end-first);
    }
    // Compatibility with pre-0.6 rows: after "pid=<digits> " a plugin row
    // started with its Mod ID, while a Base row started with free-form text.
    const auto pid=line.find(" pid=");
    if(pid!=std::string::npos){
        const auto first=line.find(' ',pid+5);
        if(first!=std::string::npos){
            const auto token_first=first+1,token_end=line.find(' ',token_first);
            const auto token=line.substr(token_first,token_end-token_first);
            if(known_mods.count(token))return token;
        }
    }
    return "base";
}

inline Result export_file(const fs::path& input,const fs::path& output,
                          const std::set<std::string>& selected,
                          const std::set<std::string>& known_mods,
                          const std::string& header) {
    if(selected.empty())throw std::runtime_error("No log source selected");
    if(fs::absolute(input).lexically_normal()==fs::absolute(output).lexically_normal())throw std::runtime_error("Cannot overwrite the live runtime log");
    std::ifstream source(input,std::ios::binary);
    if(!source)throw std::runtime_error("Runtime log does not exist");
    std::ofstream target(output,std::ios::binary|std::ios::trunc);
    if(!target)throw std::runtime_error("Cannot create exported log");
    target<<header;if(!header.empty()&&header.back()!='\n')target<<'\n';
    Result result;std::string line;
    while(std::getline(source,line)){
        ++result.inspected;if(!line.empty()&&line.back()=='\r')line.pop_back();
        if(selected.count(source_of(line,known_mods))){target<<line<<'\n';++result.exported;}
    }
    if(!source.eof())throw std::runtime_error("Cannot read runtime log");
    if(!target)throw std::runtime_error("Cannot write exported log");
    return result;
}
}
