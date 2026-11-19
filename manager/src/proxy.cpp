#include "common.h"
#include <unknwn.h>
extern DWORD WINAPI StartSan14Host(void*);
static HMODULE self_module;
static INIT_ONCE once=INIT_ONCE_STATIC_INIT;
static HMODULE provider;
static BOOL CALLBACK load_provider(PINIT_ONCE,void*,void**){wchar_t path[MAX_PATH];UINT n=GetSystemDirectoryW(path,MAX_PATH);if(!n||n+13>=MAX_PATH)return FALSE;wcscat_s(path,L"\\dinput8.dll");provider=LoadLibraryExW(path,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);return provider!=nullptr;}
static FARPROC original(const char* n){return InitOnceExecuteOnce(&once,load_provider,nullptr,nullptr)?GetProcAddress(provider,n):nullptr;}
extern "C" HRESULT WINAPI ProxyDirectInput8Create(HINSTANCE h,DWORD v,REFIID iid,void** out,LPUNKNOWN outer){using Fn=HRESULT(WINAPI*)(HINSTANCE,DWORD,REFIID,void**,LPUNKNOWN);auto f=(Fn)original("DirectInput8Create");return f?f(h,v,iid,out,outer):E_FAIL;}
extern "C" HRESULT WINAPI ProxyDllCanUnloadNow(){return S_FALSE;}
extern "C" HRESULT WINAPI ProxyDllGetClassObject(REFCLSID c,REFIID i,void** out){using Fn=HRESULT(WINAPI*)(REFCLSID,REFIID,void**);auto f=(Fn)original("DllGetClassObject");return f?f(c,i,out):CLASS_E_CLASSNOTAVAILABLE;}
extern "C" HRESULT WINAPI ProxyDllRegisterServer(){using Fn=HRESULT(WINAPI*)();auto f=(Fn)original("DllRegisterServer");return f?f():E_NOTIMPL;}
extern "C" HRESULT WINAPI ProxyDllUnregisterServer(){using Fn=HRESULT(WINAPI*)();auto f=(Fn)original("DllUnregisterServer");return f?f():E_NOTIMPL;}
#pragma comment(linker,"/EXPORT:DirectInput8Create=ProxyDirectInput8Create")
#pragma comment(linker,"/EXPORT:DllCanUnloadNow=ProxyDllCanUnloadNow")
#pragma comment(linker,"/EXPORT:DllGetClassObject=ProxyDllGetClassObject")
#pragma comment(linker,"/EXPORT:DllRegisterServer=ProxyDllRegisterServer")
#pragma comment(linker,"/EXPORT:DllUnregisterServer=ProxyDllUnregisterServer")
BOOL WINAPI DllMain(HINSTANCE h,DWORD reason,void*){if(reason==DLL_PROCESS_ATTACH){self_module=h;DisableThreadLibraryCalls(h);HANDLE worker=CreateThread(nullptr,0,StartSan14Host,h,0,nullptr);if(worker)CloseHandle(worker);}return TRUE;}
