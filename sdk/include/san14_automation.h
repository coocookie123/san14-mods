#ifndef SAN14_MOD_SDK_AUTOMATION_H
#define SAN14_MOD_SDK_AUTOMATION_H
#include "san14_types.h"

typedef San14Handle San14AutomationTask;
/* Optional shared Base service. Calls are UI-thread/nonblocking and owner-bound.
 * submit copies the UTF-8 method and JSON object before returning. Only packaged
 * tool names (e.g. get_overview or advance_time) are accepted as method; the
 * internal tool.call/tool.list transport names are not public SDK methods.
 * Never accepts JavaScript, addresses or executable code.
 * Permission checks follow the method's query/command classification. Starting
 * the shared worker does not require any optional MCP listener plugin to be enabled. */
typedef struct San14AutomationApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *submit)(void* host_context,
        San14StringView method,San14StringView parameters_json,San14AutomationTask* out_task);
    /* Pending: OK, complete=0, required=0, no buffer changes. Completed results,
     * including operation failures, are UTF-8 JSON with complete=1. required
     * includes the terminating NUL; a null/short buffer returns BUFFER_TOO_SMALL
     * without consuming or partially writing the result. Poll may be repeated. */
    San14Result (SAN14_MOD_CALL *poll)(void* host_context,San14AutomationTask task,
        char* buffer,uint32_t capacity,uint32_t* required,int32_t* complete);
    /* Releases the result and cancels work that has not begun when possible.
     * This is not rollback for a native mutation that has already executed. */
    San14Result (SAN14_MOD_CALL *release)(void* host_context,San14AutomationTask task);
} San14AutomationApi;

#define SAN14_MCP_DEFAULT_PORT UINT32_C(8765)
typedef enum San14McpState {
    SAN14_MCP_STOPPED=0,
    SAN14_MCP_STARTING=1,
    SAN14_MCP_RUNNING=2,
    SAN14_MCP_STOPPING=3,
    SAN14_MCP_FAILED=4
} San14McpState;
typedef struct San14McpConfig {
    uint32_t struct_size;
    uint32_t port; /* 1..65535. Bind address is always 127.0.0.1. */
} San14McpConfig;
typedef struct San14McpStatus {
    uint32_t struct_size;
    San14McpState state;
    uint32_t port;
    char last_error[256]; /* Cached UTF-8, always NUL-terminated. */
} San14McpStatus;

/* Optional public MCP listener. Base owns the shared worker independently of
 * this listener. Requires network, game.commands and the declared read scopes.
 * start/stop produce Automation tasks; poll/release them through automation.
 * stop first revokes new listener requests and pending mutation authorization.
 * Existing native execution must settle before STOPPED; a client Mod must not
 * report successful disable while status is STARTING/RUNNING/STOPPING. */
typedef struct San14McpApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *start)(void* host_context,
        const San14McpConfig* config,San14AutomationTask* out_task);
    San14Result (SAN14_MOD_CALL *stop)(void* host_context,San14AutomationTask* out_task);
    /* Copies current cached state; does not wait for or invoke game work. */
    San14Result (SAN14_MOD_CALL *status)(void* host_context,San14McpStatus* out_status);
} San14McpApi;
#endif
