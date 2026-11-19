#pragma once
#include "game_adapter_api.h"
namespace manager_core {
const San14ManagerCoreApi* api() noexcept;
San14Result SAN14_ADAPTER_CALL worker_open(const San14CoreWorkerOptions*, void**, char*, uint32_t);
San14Result SAN14_ADAPTER_CALL worker_request(void*, const char*, const char*, uint32_t, San14CoreWorkerReply, void*);
San14Result SAN14_ADAPTER_CALL worker_close(void*, uint32_t);
}
