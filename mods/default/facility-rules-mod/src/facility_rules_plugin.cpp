#include <cstddef>
#include <cstring>

#include "san14_mod.h"

namespace {
const San14HostApi* host = nullptr;
bool active = false;

San14StringView sv(const char* text) {
    return {text, static_cast<uint32_t>(std::strlen(text))};
}

// Each switch maps to one named rule. The GameAdapter owns the native change
// and returns SAN14_E_UNSUPPORTED on builds it has not verified.
struct Feature {
    const char* key;
    const char* label;
    const char* help;
    const char* rule;
    const char* category;
};

const Feature features[] = {
    {"trap_adjacent_trigger", u8"陷阱近身触发",
     u8"原版的陷阱、落石、猛火陷阱只要敌军进入 2 格内就发动，常常是打头的部队刚碰到范围边缘就触发，后面的部队还没进效果范围。"
     u8"开启后要敌军走到陷阱所在格或紧挨的 6 格内才发动；火陷阱、火船本来就是 1 格，不变。"
     u8"伤害、状态异常和效果范围照原版，双方的陷阱一样处理，设施说明里的发动范围也显示 1。",
     "facilities.traps.adjacent_trigger", u8"陷阱"},
};
constexpr size_t feature_count = sizeof(features) / sizeof(features[0]);
static_assert(feature_count == 1, "Facility rules mod's supported feature set");
bool applied[feature_count]{};

San14Result apply(size_t index, bool value) {
    if (applied[index] == value) return SAN14_OK;
    const auto result = host->rules.set_bool(host->host_context, sv(features[index].rule), value ? 1 : 0);
    if (result != SAN14_OK) return result;
    applied[index] = value;
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL changed(void*, San14StringView key, int32_t value) {
    if (!active) return SAN14_E_BUSY;
    if (!key.data || (value != 0 && value != 1)) return SAN14_E_INVALID_ARGUMENT;
    for (size_t i = 0; i < feature_count; ++i) {
        if (key.length == std::strlen(features[i].key) &&
            std::memcmp(key.data, features[i].key, key.length) == 0) {
            return apply(i, value != 0);
        }
    }
    return SAN14_E_NOT_FOUND;
}

San14Result SAN14_MOD_CALL load(San14ModContext*) {
    for (const auto& feature : features) {
        const San14BoolSetting spec{sizeof(spec), sv(feature.key), sv(feature.label), sv(feature.help), 0, changed, nullptr, sv(feature.category), 0};
        const auto result = host->settings.register_bool(host->host_context, &spec);
        if (result != SAN14_OK) return result;
    }
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL enable(San14ModContext*) {
    active = true;
    for (size_t i = 0; i < feature_count; ++i) {
        int32_t value = 0;
        const auto result = host->settings.get_bool(host->host_context, sv(features[i].key), &value);
        if (result != SAN14_OK) return result;
        if (value) {
            const auto applied_result = apply(i, true);
            if (applied_result != SAN14_OK) return applied_result;
        }
    }
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL disable(San14ModContext*) {
    bool busy = false;
    for (size_t i = feature_count; i > 0; --i) {
        if (applied[i - 1] && apply(i - 1, false) != SAN14_OK) busy = true;
    }
    if (busy) return SAN14_E_BUSY;
    active = false;
    return SAN14_OK;
}

void SAN14_MOD_CALL unload(San14ModContext*) {
    active = false;
    host = nullptr;
    for (auto& value : applied) value = false;
}
}  // namespace

extern "C" SAN14_MOD_EXPORT San14Result SAN14_MOD_CALL SAN14Mod_Initialize(
    const San14HostApi* h,
    San14ModApi* out
) {
    if (!h || !out || h->abi_version != SAN14_HOST_ABI_V1 || out->struct_size < sizeof(*out) ||
        h->struct_size < offsetof(San14HostApi, settings) + sizeof(h->settings) ||
        h->rules.struct_size < sizeof(San14RuleApi) || h->settings.struct_size < sizeof(San14SettingsApi) ||
        !h->rules.set_bool || !h->settings.register_bool || !h->settings.get_bool) {
        return SAN14_E_UNSUPPORTED;
    }
    if (active) return SAN14_E_BUSY;
    host = h;
    *out = {sizeof(*out), SAN14_HOST_ABI_V1, load, enable, disable, unload};
    return SAN14_OK;
}
