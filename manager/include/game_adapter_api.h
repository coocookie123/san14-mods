#ifndef SAN14_GAME_ADAPTER_API_H
#define SAN14_GAME_ADAPTER_API_H
#include <stdint.h>
#include <wchar.h>
#include "san14_mod.h"

#define SAN14_GAME_ADAPTER_ABI_V1 1u
#define SAN14_MANAGER_CORE_ABI_V1 1u
#if defined(_WIN32)
#define SAN14_ADAPTER_CALL __cdecl
#else
#define SAN14_ADAPTER_CALL
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct San14CoreAdmission {
    uint32_t struct_size;
    uint32_t game_build;
    const char* host_version;
    const char* game_exe_sha256;
} San14CoreAdmission;

/* The core owns the process and the verified deployment. The adapter handles
 * semantic requests only and owns context until worker_close completes. */
typedef void (SAN14_ADAPTER_CALL *San14CoreWorkerReply)(void*, const char* json);
typedef San14Result (SAN14_ADAPTER_CALL *San14CoreWorkerDispatch)(
    void*, const char* method, const char* arguments, San14CoreWorkerReply, void*);
typedef struct San14CoreWorkerOptions {
    uint32_t struct_size;
    const wchar_t* assets_directory;
    void* context;
    San14CoreWorkerDispatch dispatch;
    int32_t (SAN14_ADAPTER_CALL *forbidden_thread)(void*);
} San14CoreWorkerOptions;

/* All strings/buffers are borrowed for a call; no C++ objects cross this ABI. */
typedef struct San14ManagerCoreApi {
    uint32_t struct_size;
    uint32_t abi_version;
    int32_t (SAN14_ADAPTER_CALL *valid_id)(const char*);
    int32_t (SAN14_ADAPTER_CALL *valid_url)(const char*);
    San14Result (SAN14_ADAPTER_CALL *version_matches)(const char*, const char*, int32_t*);
    San14Result (SAN14_ADAPTER_CALL *admit_mod)(const wchar_t*, const San14CoreAdmission*, char*, uint32_t, uint32_t*, char*, uint32_t);
    San14Result (SAN14_ADAPTER_CALL *load_plugin)(const wchar_t*, const char*, const San14HostApi*, San14ModApi*, void**);
    void (SAN14_ADAPTER_CALL *release_plugin)(void*);
    San14Result (SAN14_ADAPTER_CALL *lifecycle)(San14ModContext*, San14Result (SAN14_MOD_CALL *)(San14ModContext*));
    San14Result (SAN14_ADAPTER_CALL *setting_changed)(San14SettingChanged, void*, San14StringView, int32_t);
    San14Result (SAN14_ADAPTER_CALL *global_read)(const wchar_t*, const char*, const char*, void*, uint32_t, uint32_t*);
    San14Result (SAN14_ADAPTER_CALL *global_write)(const wchar_t*, const char*, const char*, const void*, uint32_t);
    San14Result (SAN14_ADAPTER_CALL *worker_open)(const San14CoreWorkerOptions*, void**, char*, uint32_t);
    San14Result (SAN14_ADAPTER_CALL *worker_request)(void*, const char*, const char*, uint32_t,
                                                     San14CoreWorkerReply, void*);
    San14Result (SAN14_ADAPTER_CALL *worker_close)(void*, uint32_t);
} San14ManagerCoreApi;

typedef struct San14GameAdapterStart {
    uint32_t struct_size;
    uint32_t abi_version;
    void* manager_module;
    void* adapter_module;
    const San14ManagerCoreApi* core;
} San14GameAdapterStart;

typedef struct San14GameAdapterApi {
    uint32_t struct_size;
    uint32_t abi_version;
    uint32_t sdk_abi_version;
    uint32_t required_core_size;
    const char* adapter_id;
    const char* adapter_version;
    San14Result (SAN14_ADAPTER_CALL *start)(const San14GameAdapterStart*);
} San14GameAdapterApi;

typedef San14Result (SAN14_ADAPTER_CALL *San14GameAdapterQuery)(uint32_t, San14GameAdapterApi*);
#ifdef __cplusplus
}
#endif
#endif
