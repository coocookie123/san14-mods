#ifndef SAN14_MOD_SDK_RULES_H
#define SAN14_MOD_SDK_RULES_H
#include "san14_types.h"

/* Named, build-verified rules. No addresses or native objects cross this API. */
typedef struct San14RuleApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *set_bool)(void* host_context, San14StringView rule_id, int32_t enabled);
} San14RuleApi;

/* A failed change is not persisted and the checkbox retains its old value. */
typedef San14Result (SAN14_MOD_CALL *San14SettingChanged)(void* plugin_context, San14StringView key, int32_t value);
typedef struct San14BoolSetting {
    uint32_t struct_size;
    San14StringView key;
    San14StringView label;
    San14StringView description;
    int32_t default_value;
    San14SettingChanged changed;
    void* plugin_context;
    /* Optional UTF-8 category. Empty/legacy specs use 常规; one category hides tabs. */
    San14StringView category;
    /* Optional bitmask of SAN14_BOOL_SETTING_* flags (requires V2+ flags size). */
    uint32_t flags;
} San14BoolSetting;
#define SAN14_BOOL_SETTING_V1_SIZE offsetof(San14BoolSetting, category)
#define SAN14_BOOL_SETTING_V2_SIZE offsetof(San14BoolSetting, flags)
#define SAN14_BOOL_SETTING_DISABLED 1u
/* Persist desired value without invoking changed on a running plugin.
 * The plugin reads the preference during its next enable; use a restart-only
 * Mod when the feature must stay unchanged until the next game launch. */
#define SAN14_BOOL_SETTING_RESTART_REQUIRED 2u
#define SAN14_SETTING_DISABLED SAN14_BOOL_SETTING_DISABLED
typedef struct San14SettingsApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *register_bool)(void* host_context, const San14BoolSetting* spec);
    San14Result (SAN14_MOD_CALL *get_bool)(void* host_context, San14StringView key, int32_t* out_value);
} San14SettingsApi;

/* Stepped range/slider setting rendered in the standard Mod settings page and persisted
 * as an integer in settings.ini. Optional tail service via San14HostApi.settings_ext. */
typedef struct San14RangeSetting {
    uint32_t struct_size;
    San14StringView key;
    San14StringView label;
    San14StringView description;
    int32_t min_value;
    int32_t max_value;
    int32_t step;
    int32_t default_value;
    /* Display divisor (>= 1): e.g. value 250 / divisor 100 formats as "2.50". */
    int32_t display_divisor;
    /* Optional UTF-8 suffix appended to formatted value (e.g. "x"). */
    San14StringView unit_suffix;
    San14SettingChanged changed;
    void* plugin_context;
    San14StringView category;
    uint32_t flags;
} San14RangeSetting;

typedef struct San14SettingsExtendedApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *register_range)(void* host_context, const San14RangeSetting* spec);
    San14Result (SAN14_MOD_CALL *get_range)(void* host_context, San14StringView key, int32_t* out_value);
} San14SettingsExtendedApi;
#endif
