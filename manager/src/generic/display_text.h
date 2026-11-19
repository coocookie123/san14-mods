#pragma once
#include <windows.h>
#include <string>

// Display captions only. Never convert paths, identifiers, clipboard contents
// or persisted values to match a game's font atlas.
namespace display_text {
inline bool traditional=false;
inline const wchar_t* manager_label() noexcept {return traditional?L"模組管理":L"模组管理";}
inline const wchar_t* operation_label(bool manual) noexcept {
    if(manual)return traditional?L"手動操作":L"手动操作";
    return traditional?L"自動操作":L"自动操作";
}
inline const wchar_t* force_control_label() noexcept {return traditional?L"操作勢力":L"操作势力";}
inline const wchar_t* manager_help() noexcept {
    return traditional?L"打開模組管理，啟用、停用或設定模組。":L"打开模组管理，启用、禁用或设置模组。";
}
inline std::wstring caption(const std::wstring& source){
    if(!traditional||source.empty())return source;
    const int size=LCMapStringEx(L"zh-TW",LCMAP_TRADITIONAL_CHINESE,source.data(),static_cast<int>(source.size()),nullptr,0,nullptr,nullptr,0);
    if(size<=0)return source;
    std::wstring result(static_cast<size_t>(size),L'\0');
    if(!LCMapStringEx(L"zh-TW",LCMAP_TRADITIONAL_CHINESE,source.data(),static_cast<int>(source.size()),result.data(),size,nullptr,nullptr,0))return source;
    return result;
}
}
