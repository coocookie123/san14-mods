#include <cstddef>
#include <cstring>

#include "san14_mod.h"
#include "diplomacy_policy.h"
#include "tactical_ai_policy.h"
#include "government_policy.h"

namespace {
const San14HostApi* host = nullptr;
bool active = false;
bool diplomacy_registered = false;
bool tactical_registered = false;
bool government_registered = false;
bool office_schedule_applied = false;

companion_mod::diplomacy_policy::State diplomacy_state;
companion_mod::tactical_ai_policy::State tactical_state;

San14StringView sv(const char* text) {
    return {text, static_cast<uint32_t>(std::strlen(text))};
}

enum class Service { Rule, Diplomacy, Tactical, Government };

struct Feature {
    const char* key;
    const char* label;
    const char* help;
    const char* rule;
    Service service;
};

const Feature features[] = {
    {"fix_ai_gate_siege", u8"优化电脑攻城编组：更常使用攻城器械，并提高单支器械队兵力", u8"提高电脑单一出征队列上限并优化攻城队配比。", "ai.siege.composition", Service::Rule},
    {"dynamic_diplomacy", u8"动态外交（类似群雄策 MOD）：电脑会选择一个主要敌人进攻，并尽量与其他势力结盟", u8"依据威胁与敌意动态判定主敌；对共敌势力放宽同盟与停战条件，对互敌势力提高结盟阻力。", "diplomacy.ai.dynamic", Service::Diplomacy},
    {"smart_district_ai", u8"优化孙权等南方势力的水战逻辑：防守时主动出击在水上解决战斗", u8"检测水域来袭与河道威胁，择优推举水战/操舵武将并调度舰队拦截或机动防御。", "ai.tactical.policy", Service::Tactical},
    {"emergency_reinforcement_ai", u8"优化电脑出兵、回防和邻城救援逻辑", u8"重估支援兵力并保障前线安全留兵，在战局吃紧时触发远征召回、邻城强援及防止过早撤退。", "ai.tactical.policy", Service::Tactical},
    {"siege_interception_ai", u8"电脑防守时会主动出击攻击攻城器械", u8"基于行军时间窗口预警敌方攻城器械并统筹前线迎击。", "ai.tactical.policy", Service::Tactical},
    {"weighted_government_auto_assign", u8"优化电脑施政", u8"根据兵力、战线、粮草和水域威胁调整自动施政排序。", "government.ai.optimized", Service::Government}
};
constexpr size_t feature_count = sizeof(features) / sizeof(features[0]);
static_assert(feature_count == 6, "AI IQ mod's supported feature set");
bool applied[feature_count]{};

bool government_available() {
    return host && host->struct_size >= offsetof(San14HostApi, government) + sizeof(host->government) &&
           host->government && host->government->struct_size >= sizeof(San14GovernmentApi) &&
           host->government->register_policy && host->government->unregister_policy;
}

bool diplomacy_available() {
    return host &&
           host->struct_size >= offsetof(San14HostApi, diplomacy) + sizeof(host->diplomacy) &&
           host->diplomacy &&
           host->diplomacy->struct_size >= sizeof(San14DiplomacyApi) &&
           host->diplomacy->register_policy &&
           host->diplomacy->unregister_policy;
}

bool tactical_ai_available() {
    return host &&
           host->struct_size >= offsetof(San14HostApi, tactical_ai) + sizeof(host->tactical_ai) &&
           host->tactical_ai &&
           host->tactical_ai->struct_size >= sizeof(San14TacticalAiApi) &&
           host->tactical_ai->register_policy &&
           host->tactical_ai->unregister_policy;
}

companion_mod::tactical_ai_policy::Config tactical_config_with(size_t override_index, bool override_value) {
    companion_mod::tactical_ai_policy::Config cfg{};
    for (size_t i = 0; i < feature_count; ++i) {
        const bool v = (i == override_index) ? override_value : applied[i];
        if (!v) continue;
        if (std::strcmp(features[i].key, "smart_district_ai") == 0) cfg.naval_defense = true;
        else if (std::strcmp(features[i].key, "emergency_reinforcement_ai") == 0) cfg.emergency_reinforcement = true;
        else if (std::strcmp(features[i].key, "siege_interception_ai") == 0) cfg.siege_interception = true;
    }
    return cfg;
}

San14Result ensure_policy(Service service) {
    if (service == Service::Diplomacy && !diplomacy_registered) {
        if (!diplomacy_available()) return SAN14_E_UNSUPPORTED;
        const auto spec = companion_mod::diplomacy_policy::make_policy(diplomacy_state);
        const auto result = host->diplomacy->register_policy(host->host_context, &spec);
        if (result != SAN14_OK) return result;
        diplomacy_registered = true;
    } else if (service == Service::Tactical && !tactical_registered) {
        if (!tactical_ai_available()) return SAN14_E_UNSUPPORTED;
        const auto spec = companion_mod::tactical_ai_policy::make_policy(tactical_state);
        const auto result = host->tactical_ai->register_policy(host->host_context, &spec);
        if (result != SAN14_OK) return result;
        tactical_registered = true;
    }
    if (service == Service::Government && !government_registered) {
        if (!government_available()) return SAN14_E_UNSUPPORTED;
        const auto result = host->government->register_policy(host->host_context, companion_government_policy::score, nullptr);
        if (result != SAN14_OK) return result;
        government_registered = true;
    }
    return SAN14_OK;
}

San14Result apply(size_t index, bool value) {
    const auto& feature = features[index];
    if (applied[index] == value) return SAN14_OK;

    if (feature.service == Service::Tactical) {
        const auto prev_cfg = tactical_config_with(feature_count, false);
        const auto next_cfg = tactical_config_with(index, value);
        if (value) {
            const auto ready = ensure_policy(Service::Tactical);
            if (ready != SAN14_OK) return ready;
        }
        tactical_state.set_config(next_cfg);
        if (prev_cfg.any_enabled() != next_cfg.any_enabled()) {
            const auto rule_res = host->rules.set_bool(host->host_context, sv(feature.rule), next_cfg.any_enabled() ? 1 : 0);
            if (rule_res != SAN14_OK) {
                tactical_state.set_config(prev_cfg);
                return rule_res;
            }
        }
        applied[index] = value;
        return SAN14_OK;
    }

    if (value) {
        const auto ready = ensure_policy(feature.service);
        if (ready != SAN14_OK) return ready;
    }
    if (feature.service == Service::Diplomacy) diplomacy_state.set_enabled(value);

    const auto result = host->rules.set_bool(host->host_context, sv(feature.rule), value ? 1 : 0);
    if (result != SAN14_OK) {
        if (feature.service == Service::Diplomacy) diplomacy_state.set_enabled(applied[index]);
        return result;
    }
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
        const San14BoolSetting spec{sizeof(spec), sv(feature.key), sv(feature.label), sv(feature.help), 0, changed, nullptr, sv(u8"常规"), 0};
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
    const auto schedule_result = host->rules.set_bool(host->host_context, sv("ai.office.semiannual"), 1);
    if (schedule_result != SAN14_OK) return schedule_result;
    office_schedule_applied = true;
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL disable(San14ModContext*) {
    bool busy = false;
    if (office_schedule_applied) {
        if (host->rules.set_bool(host->host_context, sv("ai.office.semiannual"), 0) == SAN14_OK) office_schedule_applied = false;
        else busy = true;
    }
    for (size_t i = feature_count; i > 0; --i) {
        if (applied[i - 1] && apply(i - 1, false) != SAN14_OK) busy = true;
    }
    bool diplomacy_applied = false;
    bool tactical_applied = false;
    bool government_applied = false;
    for (size_t i = 0; i < feature_count; ++i) {
        if (features[i].service == Service::Diplomacy) diplomacy_applied = applied[i];
        else if (features[i].service == Service::Tactical && applied[i]) tactical_applied = true;
        else if (features[i].service == Service::Government) government_applied = applied[i];
    }
    if (diplomacy_registered && !diplomacy_applied) {
        if (host->diplomacy->unregister_policy(host->host_context) == SAN14_OK) diplomacy_registered = false;
        else busy = true;
    }
    if (tactical_registered && !tactical_applied) {
        if (host->tactical_ai->unregister_policy(host->host_context) == SAN14_OK) tactical_registered = false;
        else busy = true;
    }
    if (government_registered && !government_applied) {
        if (host->government->unregister_policy(host->host_context) == SAN14_OK) government_registered = false;
        else busy = true;
    }
    if (busy) return SAN14_E_BUSY;
    active = false;
    return SAN14_OK;
}

void SAN14_MOD_CALL unload(San14ModContext*) {
    active = false;
    host = nullptr;
    diplomacy_registered = false;
    tactical_registered = false;
    government_registered = false;
    office_schedule_applied = false;
    diplomacy_state.set_enabled(false);
    tactical_state.set_config({});
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
