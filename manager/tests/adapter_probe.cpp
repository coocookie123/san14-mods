#include "adapter_loader.h"
#include "core_services.h"
#include <iostream>
int wmain(int argc,wchar_t** argv){
    try{if(argc<2)return 2;auto loaded=manager_core::load_adapter(argv[1]);std::cout<<"GameAdapter identity and binary contract passed: "<<loaded.api.adapter_id<<"\n";
        if(argc>2&&std::wstring(argv[2])==L"--start-fixture"){auto r=manager_core::start_adapter(loaded,GetModuleHandleW(nullptr));if(r!=SAN14_OK)return 3;std::cout<<"Fixture start and core service callbacks passed\n";}
        return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
