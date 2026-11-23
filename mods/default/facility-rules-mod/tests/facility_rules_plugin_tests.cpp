#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "san14_mod.h"
#include <cassert>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {
const std::string trap_rule = "facilities.traps.adjacent_trigger";
std::vector<San14BoolSetting> settings;
std::map<std::string, int> values, rules, rule_calls;
San14Result rule_result = SAN14_OK;

std::string key(San14StringView s) { return {s.data, s.length}; }
San14Result SAN14_MOD_CALL reg(void*, const San14BoolSetting* s) { settings.push_back(*s); return SAN14_OK; }
San14Result SAN14_MOD_CALL get(void*, San14StringView k, int32_t* out) { *out = values[key(k)]; return SAN14_OK; }
San14Result SAN14_MOD_CALL rule(void*, San14StringView k, int32_t v) {
    const auto name = key(k);
    ++rule_calls[name];
    if (rule_result != SAN14_OK) return rule_result;
    rules[name] = v;
    return SAN14_OK;
}

void reset() { settings.clear(); values.clear(); rules.clear(); rule_calls.clear(); rule_result = SAN14_OK; }

San14HostApi fake_host() {
    San14HostApi host{};
    host.struct_size = sizeof(host);
    host.abi_version = SAN14_HOST_ABI_V1;
    host.rules = {sizeof(host.rules), rule};
    host.settings = {sizeof(host.settings), reg, get};
    return host;
}

San14Result change(const std::string& name, int32_t value) {
    for (auto& s : settings) if (key(s.key) == name) return s.changed(s.plugin_context, s.key, value);
    assert(false);
    return SAN14_E_NOT_FOUND;
}

using Initialize = San14Result (SAN14_MOD_CALL*)(const San14HostApi*, San14ModApi*);
}  // namespace

int main() {
    wchar_t exe_path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    const auto beside_exe = (length > 0 && length < MAX_PATH)
        ? std::filesystem::path(exe_path).parent_path() / L"facility_rules.dll"
        : std::filesystem::path(L"facility_rules.dll");
    auto dll = LoadLibraryExW(beside_exe.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    assert(dll);
    auto init = reinterpret_cast<Initialize>(GetProcAddress(dll, "SAN14Mod_Initialize"));
    assert(init);

    San14ModApi api{}; api.struct_size = sizeof(api);
    San14ModContext context{}; context.struct_size = sizeof(context);
    {
        auto broken = fake_host(); broken.rules.set_bool = nullptr;
        assert(init(&broken, &api) == SAN14_E_UNSUPPORTED);
    }

    // Registration: one default-off switch on its own page.
    reset();
    auto host = fake_host();
    assert(init(&host, &api) == SAN14_OK);
    assert(api.on_load(&context) == SAN14_OK && settings.size() == 1);
    const auto& trap = settings[0];
    assert(key(trap.key) == "trap_adjacent_trigger" && key(trap.label) == u8"陷阱近身触发");
    assert(key(trap.category) == u8"陷阱" && trap.default_value == 0 && trap.flags == 0 && trap.description.length > 0);
    assert(change("trap_adjacent_trigger", 1) == SAN14_E_BUSY && rule_calls.empty());

    // A stored "off" preference leaves the native rule untouched.
    assert(api.on_enable(&context) == SAN14_OK && rule_calls.empty());
    assert(change("trap_adjacent_trigger", 1) == SAN14_OK && rules.at(trap_rule) == 1);
    assert(change("trap_adjacent_trigger", 1) == SAN14_OK && rule_calls.at(trap_rule) == 1);
    assert(change("trap_adjacent_trigger", 0) == SAN14_OK && rules.at(trap_rule) == 0);
    assert(change("trap_adjacent_trigger", 2) == SAN14_E_INVALID_ARGUMENT);
    assert(settings[0].changed(nullptr, {"unknown", 7}, 1) == SAN14_E_NOT_FOUND);

    // Builds without the adapter rule refuse the switch; nothing is applied.
    rule_result = SAN14_E_UNSUPPORTED;
    assert(change("trap_adjacent_trigger", 1) == SAN14_E_UNSUPPORTED && rules.at(trap_rule) == 0);
    rule_result = SAN14_OK;
    const auto calls = rule_calls.at(trap_rule);
    assert(api.on_disable(&context) == SAN14_OK && rule_calls.at(trap_rule) == calls);
    api.on_unload(&context);

    // A stored "on" preference is applied on enable and released on disable.
    reset();
    values["trap_adjacent_trigger"] = 1;
    assert(init(&host, &api) == SAN14_OK && api.on_load(&context) == SAN14_OK);
    assert(api.on_enable(&context) == SAN14_OK && rules.at(trap_rule) == 1);
    rule_result = SAN14_E_BUSY;
    assert(api.on_disable(&context) == SAN14_E_BUSY && rules.at(trap_rule) == 1);
    assert(change("trap_adjacent_trigger", 0) == SAN14_E_BUSY);
    rule_result = SAN14_OK;
    assert(api.on_disable(&context) == SAN14_OK && rules.at(trap_rule) == 0);
    api.on_unload(&context);

    // An unsupported build fails enable only when the user had turned the switch on.
    reset();
    values["trap_adjacent_trigger"] = 1;
    rule_result = SAN14_E_UNSUPPORTED;
    assert(init(&host, &api) == SAN14_OK && api.on_load(&context) == SAN14_OK);
    assert(api.on_enable(&context) == SAN14_E_UNSUPPORTED && rules.empty());
    rule_result = SAN14_OK;
    assert(api.on_disable(&context) == SAN14_OK && rules.empty());
    api.on_unload(&context);

    FreeLibrary(dll);
    std::cout << "Facility rules: registration, trap rule on/off, unsupported build, busy disable and reload passed\n";
}
