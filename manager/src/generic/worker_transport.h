#pragma once
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace worker_transport {
class RpcError:public std::runtime_error {
public:
    std::string code;
    RpcError(std::string error_code,const std::string& message):std::runtime_error(message),code(std::move(error_code)){}
};
struct Options {
    // Immutable, trusted Base configuration, never values from a plugin or an
    // incoming RPC. Base admits the packaged worker and its dependencies first.
    std::wstring worker_exe;
    std::vector<std::wstring> worker_args;
    std::wstring cwd;
    uint32_t parent_pid=0; // 0 selects this process; any other PID is rejected
    void* user=nullptr;
    std::string (*dispatch)(void*,const std::string& method,const std::string& params_json)=nullptr;
    bool (*forbidden_thread)(void*)=nullptr;
    uint32_t startup_timeout_ms=10000;
    // Keeps callback state alive while an interrupted session drains.
    std::shared_ptr<void> user_lifetime;
};
class Transport {
public:
    explicit Transport(Options options);
    ~Transport();
    Transport(const Transport&)=delete;
    Transport& operator=(const Transport&)=delete;
    bool start(std::string& error);
    // Params must be an object. Returns the peer's result JSON, or a receipt
    // with transport_error=true. Timeout/disconnection is uncertain: the remote
    // operation is not canceled or resent, and late replies are only retired.
    std::string request(const std::string& method,const std::string& params_json,uint32_t timeout_ms);
    // Sends worker.stop, then waits for graceful exit and handlers to drain.
    // Busy/timeout retains the child and its Job. Keep user/dispatch alive until
    // this succeeds. Destruction is a final abort, closing the kill-on-close Job.
    bool stop(uint32_t timeout_ms,std::string& error);
    std::string status() const;
private:
    struct Impl;
    std::shared_ptr<Impl> impl_;
};
}
