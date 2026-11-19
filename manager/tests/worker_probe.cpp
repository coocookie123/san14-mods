#include "common.h"
#include "core_services.h"
#include "wire_json.h"
#include <chrono>
#include <iostream>
#include <thread>

namespace {
void SAN14_ADAPTER_CALL receive(void* sink, const char* text) {
    *static_cast<std::string*>(sink) = text;
}
int32_t SAN14_ADAPTER_CALL allowed(void*) { return 0; }

San14Result SAN14_ADAPTER_CALL dispatch(void*, const char* method, const char* arguments,
                                        San14CoreWorkerReply reply, void* sink) {
    try {
        const auto params = WireJson::parse(arguments);
        if (params.at("version") != 1) return SAN14_E_UNSUPPORTED;
        WireJson result;
        const std::string name = method;
        if (name == "semantic.catalog") {
            result = WireJson::parse(R"({"version":1,"native_only":true,"tools":[{
                "name":"fixture_city","description":"Read a fixture city",
                "inputSchema":{"type":"object","properties":{"city_id":{"type":"integer"}},
                               "required":["city_id"],"additionalProperties":false},
                "annotations":{"readOnlyHint":true}
            }]})");
        } else if (name == "semantic.invoke" && params.at("name") == "fixture_city" &&
                   params.at("authority") == "a001") {
            const auto input = params.at("arguments").at("city_id");
            if (!input.is_number_integer()) return SAN14_E_INVALID_ARGUMENT;
            result = {{"content", WireJson::array({{{"type", "text"}, {"text", "Fixture city"}}})},
                      {"structuredContent", {{"city_id", input}, {"name", "Fixture city"}}}};
        } else if (name == "semantic.status") {
            result = {{"runtime_busy", false}};
        } else {
            return SAN14_E_UNSUPPORTED;
        }
        const auto serialized = result.dump();
        reply(sink, serialized.c_str());
        return SAN14_OK;
    } catch (...) {
        return SAN14_E_INVALID_ARGUMENT;
    }
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    const auto core = manager_core::api();
    const San14CoreWorkerOptions settings{sizeof(settings), argv[1], nullptr, dispatch, allowed};
    void* worker = nullptr;
    char error[512]{};
    if (core->worker_open(&settings, &worker, error, sizeof(error)) != SAN14_OK) {
        std::cerr << error << '\n';
        return 3;
    }
    const auto request = [&](const char* method, const WireJson& args) {
        std::string result;
        if (core->worker_request(worker, method, args.dump().c_str(), 30000, receive, &result) != SAN14_OK)
            throw std::runtime_error("Worker request failed");
        return WireJson::parse(result);
    };
    int exit = 0;
    try {
        const auto no_args = WireJson::object();
        const auto tools = request("tool.list", no_args);
        if (tools.at("tools").size() != 1 || tools.at("tools").at(0).at("name") != "fixture_city")
            throw std::runtime_error("Invalid fixture catalogue");
        auto state = request("mcp.start", {{"host", "127.0.0.1"}, {"port", 8765}, {"authority", "a001"}});
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
        while (state.at("running") != true) {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error(state.dump());
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            state = request("mcp.status", no_args);
        }
        std::cout << state.at("port").get<int>() << std::endl;
        std::string command;
        std::getline(std::cin, command);
        if (command != "verified") throw std::runtime_error("Fixture HTTP client did not verify the tool call");
        request("mcp.stop", no_args);
    } catch (const std::exception& exception) {
        std::cerr << exception.what() << '\n';
        exit = 4;
    }
    return core->worker_close(worker, 12000) == SAN14_OK ? exit : 5;
}
