#pragma once
#include "common.h"
#include <shobjidl.h>
#include <shellapi.h>
#include <thread>
#include <exception>

namespace mod_recycle {
// Refuse a permanent-delete fallback, including when the bin cannot accept an item.
class Sink final : public IFileOperationProgressSink {
    LONG refs=1;
public:
    bool recycled=false;
    HRESULT result=E_FAIL;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void** out) override {
        if(!out)return E_POINTER;*out=nullptr;
        if(id==__uuidof(IUnknown)||id==__uuidof(IFileOperationProgressSink)){*out=static_cast<IFileOperationProgressSink*>(this);AddRef();return S_OK;}return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override{return InterlockedIncrement(&refs);}
    ULONG STDMETHODCALLTYPE Release() override{auto n=InterlockedDecrement(&refs);if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE StartOperations() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE FinishOperations(HRESULT) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreRenameItem(DWORD,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostRenameItem(DWORD,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostMoveItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostCopyItem(DWORD,IShellItem*,IShellItem*,LPCWSTR,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PreDeleteItem(DWORD flags,IShellItem*) override{return (flags&TSF_DELETE_RECYCLE_IF_POSSIBLE)?S_OK:E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostDeleteItem(DWORD,IShellItem*,HRESULT hr,IShellItem* item) override{result=hr;recycled=SUCCEEDED(hr)&&item;return S_OK;}
    HRESULT STDMETHODCALLTYPE PreNewItem(DWORD,IShellItem*,LPCWSTR) override{return E_ABORT;}
    HRESULT STDMETHODCALLTYPE PostNewItem(DWORD,IShellItem*,LPCWSTR,LPCWSTR,DWORD,HRESULT,IShellItem*) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE UpdateProgress(UINT,UINT) override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ResetTimer() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE PauseTimer() override{return S_OK;}
    HRESULT STDMETHODCALLTYPE ResumeTimer() override{return S_OK;}
};
inline void check(HRESULT hr){if(FAILED(hr))throw std::runtime_error("Windows Recycle Bin operation failed: "+std::to_string(static_cast<unsigned long>(hr)));}
inline void recycle(const fs::path& path){
    // Use a dedicated STA without changing the game's COM apartment.
    std::exception_ptr error;
    std::thread worker([&]{
        auto init=CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        IFileOperation* operation=nullptr;IShellItem* item=nullptr;auto sink=new Sink;
        try{
            check(init);check(CoCreateInstance(__uuidof(FileOperation),nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&operation)));
            check(operation->SetOperationFlags(FOF_SILENT|FOF_NOCONFIRMATION|FOF_NOERRORUI|FOFX_EARLYFAILURE|FOFX_RECYCLEONDELETE));
            check(SHCreateItemFromParsingName(path.c_str(),nullptr,IID_PPV_ARGS(&item)));
            check(operation->DeleteItem(item,sink));check(operation->PerformOperations());
            BOOL aborted=TRUE;check(operation->GetAnyOperationsAborted(&aborted));check(sink->result);
            if(aborted||!sink->recycled||fs::exists(path))throw std::runtime_error("Windows did not confirm moving the Mod to the Recycle Bin");
        }catch(...){error=std::current_exception();}
        if(item)item->Release();if(operation)operation->Release();sink->Release();if(SUCCEEDED(init))CoUninitialize();
    });worker.join();if(error)std::rethrow_exception(error);
}
}
