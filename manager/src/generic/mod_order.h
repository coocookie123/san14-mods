#pragma once
#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace mod_order {
struct Node {std::string id;std::vector<std::string> dependencies;};
struct Result {std::vector<size_t> indices,blocked;};
// Input order is the user's preference. Only dependency edges override it.
inline Result resolve(const std::vector<Node>& nodes){
    Result result;std::map<std::string,size_t> by_id;
    for(size_t i=0;i<nodes.size();++i)by_id.emplace(nodes[i].id,i);
    std::vector<bool> emitted(nodes.size(),false);
    for(size_t n=0;n<nodes.size();++n){
        size_t next=nodes.size();
        for(size_t i=0;i<nodes.size();++i){if(emitted[i])continue;bool ready=true;
            for(auto& dependency:nodes[i].dependencies){auto d=by_id.find(dependency);if(d!=by_id.end()&&!emitted[d->second]){ready=false;break;}}
            if(ready){next=i;break;}
        }
        if(next==nodes.size())break;emitted[next]=true;result.indices.push_back(next);
    }
    for(size_t i=0;i<nodes.size();++i)if(!emitted[i])result.blocked.push_back(i);
    return result;
}
// Return the first installed dependency pair violating a proposed display order.
inline std::pair<std::string,std::string> violation(const std::vector<Node>& nodes){
    std::map<std::string,size_t> positions;for(size_t i=0;i<nodes.size();++i)positions.emplace(nodes[i].id,i);
    for(size_t i=0;i<nodes.size();++i)for(auto& dep:nodes[i].dependencies){auto d=positions.find(dep);
        if(d!=positions.end()&&d->second>=i)return {dep,nodes[i].id};}
    return {};
}
}
