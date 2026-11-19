#ifndef SAN14_MOD_SDK_TYPES_H
#define SAN14_MOD_SDK_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef _WIN32
#define SAN14_MOD_EXPORT __declspec(dllexport)
#define SAN14_MOD_CALL __cdecl
#else
#define SAN14_MOD_EXPORT
#define SAN14_MOD_CALL
#endif

#define SAN14_HOST_ABI_V1 0x00010000u

typedef enum San14Result {
    SAN14_OK = 0,
    SAN14_E_INVALID_ARGUMENT = 1,
    SAN14_E_UNSUPPORTED = 2,
    SAN14_E_PERMISSION_DENIED = 3,
    SAN14_E_NOT_FOUND = 4,
    SAN14_E_WRONG_THREAD = 5,
    SAN14_E_STALE_GENERATION = 6,
    SAN14_E_BUSY = 7,
    SAN14_E_BUFFER_TOO_SMALL = 8,
    SAN14_E_PLUGIN_FAILED = 9
} San14Result;

typedef struct San14StringView {
    const char* data;
    uint32_t length;
} San14StringView;

typedef uint64_t San14Handle;
typedef San14Handle San14Subscription;
typedef San14Handle San14PanelHandle;
typedef San14Handle San14ControlHandle;
typedef San14Handle San14InputHandle;
typedef San14Handle San14TaskHandle;
typedef San14Handle San14CommandHandle;

typedef enum San14LogLevel {
    SAN14_LOG_TRACE = 0,
    SAN14_LOG_DEBUG = 1,
    SAN14_LOG_INFO = 2,
    SAN14_LOG_WARNING = 3,
    SAN14_LOG_ERROR = 4
} San14LogLevel;

typedef enum San14Thread {
    SAN14_THREAD_UI = 1,
    SAN14_THREAD_GAME = 2,
    SAN14_THREAD_WORKER = 3
} San14Thread;

typedef void (SAN14_MOD_CALL *San14TaskCallback)(void* plugin_context);

#endif
