#include "core_services.h"
#include "common.h"
#include "json.h"
#include "worker_transport.h"
#include "worker_bundle_pin.h"
#include <cstring>
#include <set>
#include <cwctype>

namespace manager_core {
namespace {
constexpr wchar_t entrypoint[] = L"san14-managed-worker.exe";
constexpr wchar_t manifest_name[] = L"worker.integrity.json";

std::wstring path_key(const fs::path& path) {
    auto name = path.generic_wstring();
    for (auto& c : name) c = static_cast<wchar_t>(towlower(c));
    return name;
}

void ensure_regular_path(const fs::path& path) {
    fs::path current;
    for (const auto& component : fs::absolute(path).lexically_normal()) {
        current /= component;
        if (!current.has_root_directory()) continue;
        const auto attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
            throw std::runtime_error("Worker path missing or reparse point refused");
    }
}

void verify_bundle(const fs::path& root) {
    ensure_regular_path(root);
    const auto manifest_path = root / manifest_name;
    ensure_regular_path(manifest_path);
    const auto bytes = read_file(manifest_path, 4 * 1024 * 1024);
    if (digest(bytes.data(), bytes.size()) != worker_bundle_manifest_sha256)
        throw std::runtime_error("Worker integrity manifest does not match this Host");
    const auto manifest = json::parse(std::string(bytes.begin(), bytes.end()));
    if (manifest.at("schema_version").integer() != 1 ||
        wide(manifest.at("entrypoint").str()) != entrypoint ||
        manifest.at("files").type != json::Value::Array)
        throw std::runtime_error("Worker integrity manifest is incompatible");
    std::set<std::wstring> listed;
    for (const auto& row : manifest.at("files").array) {
        const auto relative = fs::path(wide(row.at("path").str()));
        if (relative.empty() || relative.has_root_path())
            throw std::runtime_error("Worker paths must be relative");
        for (const auto& component : relative) {
            if (component == L"." || component == L".." ||
                component.native().find_first_of(L":\r\n") != std::wstring::npos)
                throw std::runtime_error("Invalid worker asset path");
        }
        const auto key = path_key(relative);
        if (key == manifest_name || !listed.emplace(key).second)
            throw std::runtime_error("Duplicate worker asset");
        const auto file = root / relative;
        ensure_regular_path(file);
        const auto size = row.at("size").integer();
        if (size < 0 || !fs::is_regular_file(file) ||
            fs::file_size(file) != static_cast<uint64_t>(size) ||
            hash_file(file) != row.at("sha256").str())
            throw std::runtime_error("Worker asset failed integrity verification");
    }
    if (!listed.count(entrypoint)) throw std::runtime_error("Worker executable is missing");
    for (const auto& item : fs::recursive_directory_iterator(root)) {
        ensure_regular_path(item.path());
        if (item.is_directory()) continue;
        const auto key = path_key(item.path().lexically_relative(root));
        if (!item.is_regular_file() || (key != manifest_name && !listed.count(key)))
            throw std::runtime_error("Unlisted worker asset");
    }
}

void SAN14_ADAPTER_CALL store_reply(void* output, const char* json) {
    if (!json) throw std::invalid_argument("Missing adapter reply");
    *static_cast<std::string*>(output) = json;
}

struct Worker {
    std::unique_ptr<worker_transport::Transport> transport;
};

void describe(char* error, uint32_t capacity, const char* message) noexcept {
    if (!error || !capacity) return;
    const auto length = std::min<size_t>(capacity - 1, std::strlen(message));
    std::memcpy(error, message, length);
    error[length] = 0;
}
}

San14Result SAN14_ADAPTER_CALL worker_open(const San14CoreWorkerOptions* options, void** output,
                                           char* error, uint32_t capacity) {
    if (!output) return SAN14_E_INVALID_ARGUMENT;
    *output = nullptr;
    if (!options || options->struct_size < sizeof(*options) || !options->assets_directory ||
        !options->dispatch || !options->forbidden_thread) return SAN14_E_INVALID_ARGUMENT;
    try {
        if (!worker_bundle_manifest_sha256[0])
            throw std::runtime_error("Build this Host with the intended Worker integrity manifest");
        const auto root = fs::absolute(fs::path(options->assets_directory) / L"worker").lexically_normal();
        verify_bundle(root);
        auto worker = std::make_unique<Worker>();
        auto callbacks = std::make_shared<San14CoreWorkerOptions>(*options);
        callbacks->assets_directory = nullptr;
        worker_transport::Options config;
        config.cwd = root.wstring();
        config.worker_exe = (root / entrypoint).wstring();
        config.user = callbacks.get();
        config.user_lifetime = callbacks;
        config.startup_timeout_ms = 30000;
        config.forbidden_thread = [](void* context) {
            const auto& state = *static_cast<San14CoreWorkerOptions*>(context);
            return state.forbidden_thread(state.context) != 0;
        };
        config.dispatch = [](void* context, const std::string& method, const std::string& args) {
            const auto& state = *static_cast<San14CoreWorkerOptions*>(context);
            // Raw game data and engine setup are never part of the public channel.
            if (method != "semantic.catalog" && method != "semantic.invoke" &&
                method != "semantic.status" && method != "semantic.snapshot")
                throw worker_transport::RpcError("method_not_found", "Unknown semantic Worker request");
            std::string reply;
            const auto result = state.dispatch(state.context, method.c_str(), args.c_str(), store_reply, &reply);
            if (result != SAN14_OK || reply.empty())
                throw worker_transport::RpcError("adapter_error", "GameAdapter rejected the semantic request");
            return reply;
        };
        worker->transport = std::make_unique<worker_transport::Transport>(std::move(config));
        std::string reason;
        if (!worker->transport->start(reason)) throw std::runtime_error(reason);
        *output = worker.release();
        return SAN14_OK;
    } catch (const std::exception& exception) {
        describe(error, capacity, exception.what());
        return SAN14_E_PLUGIN_FAILED;
    }
}

San14Result SAN14_ADAPTER_CALL worker_request(void* opaque, const char* method, const char* args,
                                              uint32_t timeout, San14CoreWorkerReply reply, void* context) {
    if (!opaque || !method || !args || !reply) return SAN14_E_INVALID_ARGUMENT;
    try {
        auto& worker = *static_cast<Worker*>(opaque);
        const auto result = worker.transport->request(method, args, timeout);
        reply(context, result.c_str());
        return SAN14_OK;
    } catch (...) {
        return SAN14_E_PLUGIN_FAILED;
    }
}

San14Result SAN14_ADAPTER_CALL worker_close(void* opaque, uint32_t timeout) {
    if (!opaque) return SAN14_E_INVALID_ARGUMENT;
    try {
        auto worker = static_cast<Worker*>(opaque);
        std::string error;
        if (!worker->transport->stop(timeout, error)) return SAN14_E_BUSY;
        delete worker;
        return SAN14_OK;
    } catch (...) {
        return SAN14_E_PLUGIN_FAILED;
    }
}
}
