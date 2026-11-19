#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <filesystem>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
namespace fs=std::filesystem;
inline std::wstring wide(const std::string& s){if(s.empty())return {};int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),nullptr,0);if(n<=0)throw std::runtime_error("Invalid UTF-8");std::wstring w(n,0);MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),(int)s.size(),&w[0],n);return w;}
inline std::string utf8(const std::wstring& s){if(s.empty())return {};int n=WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),nullptr,0,nullptr,nullptr);std::string v(n,0);WideCharToMultiByte(CP_UTF8,0,s.data(),(int)s.size(),&v[0],n,nullptr,nullptr);return v;}
inline fs::path module_path(HMODULE h){wchar_t b[32768];DWORD n=GetModuleFileNameW(h,b,32768);if(!n||n>=32768)throw std::runtime_error("Module path unavailable");return fs::path(std::wstring(b,n));}
inline fs::path local_data(){wchar_t b[MAX_PATH];if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,b)))throw std::runtime_error("Local AppData unavailable");return fs::path(b)/L"San14ModManager";}
inline std::vector<unsigned char> read_file(const fs::path& p,size_t limit=256*1024*1024){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("Cannot read "+utf8(p.filename().wstring()));f.seekg(0,std::ios::end);auto n=f.tellg();if(n<0||(uint64_t)n>limit)throw std::runtime_error("File too large");f.seekg(0);std::vector<unsigned char>b((size_t)n);if(n&&!f.read((char*)b.data(),n))throw std::runtime_error("Short read");return b;}
inline std::string digest(const void* data,size_t size){BCRYPT_ALG_HANDLE a=nullptr;BCRYPT_HASH_HANDLE h=nullptr;unsigned char out[32];if(size>0xffffffffu||BCryptOpenAlgorithmProvider(&a,BCRYPT_SHA256_ALGORITHM,nullptr,0)<0)throw std::runtime_error("SHA256 unavailable");bool ok=BCryptCreateHash(a,&h,nullptr,0,nullptr,0,0)>=0;if(ok)ok=BCryptHashData(h,(PUCHAR)data,(ULONG)size,0)>=0&&BCryptFinishHash(h,out,32,0)>=0;if(h)BCryptDestroyHash(h);BCryptCloseAlgorithmProvider(a,0);if(!ok)throw std::runtime_error("SHA256 failed");std::string s;static const char hex[]="0123456789abcdef";for(auto c:out){s+=hex[c>>4];s+=hex[c&15];}return s;}
inline std::string hash_file(const fs::path& p){auto b=read_file(p);return digest(b.data(),b.size());}
inline void write_atomic(const fs::path& p,const void* data,size_t size){fs::create_directories(p.parent_path());auto temp=p;temp+=L".tmp";HANDLE f=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(f==INVALID_HANDLE_VALUE)throw std::runtime_error("Cannot create temporary file");DWORD n=0;bool ok=size<=0xffffffffu&&WriteFile(f,data,(DWORD)size,&n,nullptr)&&n==size&&FlushFileBuffers(f);CloseHandle(f);if(!ok||!MoveFileExW(temp.c_str(),p.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Atomic write failed");}
inline std::string lower(std::string s){for(char& c:s)c=(char)std::tolower((unsigned char)c);return s;}
inline bool inside(const fs::path& root,const fs::path& path){auto r=fs::weakly_canonical(root);auto p=fs::weakly_canonical(path);auto a=r.begin(),b=p.begin();for(;a!=r.end();++a,++b)if(b==p.end()||_wcsicmp(a->c_str(),b->c_str()))return false;return true;}
