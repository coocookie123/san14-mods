#ifndef SAN14_MOD_SDK_SCENARIO_H
#define SAN14_MOD_SDK_SCENARIO_H
#include "san14_types.h"
enum {
    SAN14_SCENARIO_OWN_ADJACENT_AREA_EFFECT = 4,
    SAN14_SCENARIO_OTHER_ADJACENT_AREA_EFFECT = 5
};
/* Numeric scenario parameters, not arbitrary game memory. Query capabilities
   before configuring. UI thread and game.rules permission are required.
   All strings/specs are copied; a batch is atomic and exclusively owned.
   Configuration returns BUSY while the native scenario settings page exists. */
typedef enum San14ScenarioUnit {
    SAN14_SCENARIO_NATIVE = 0,
    SAN14_SCENARIO_BASE_COEFFICIENT = 1
} San14ScenarioUnit;
typedef struct San14ScenarioParameterInfo {
    uint32_t struct_size;
    uint32_t parameter_id;
    uint32_t supported_units; /* bit 1 << San14ScenarioUnit */
    int32_t native_minimum, native_maximum;
    int32_t native_defaults[5];
    int32_t coefficient_bases[5]; /* zero when unsupported */
    int32_t supported_maximum; /* validated service bound, not engine limit */
    int32_t coefficient_maximum;
} San14ScenarioParameterInfo;
typedef struct San14ScenarioParameterSpec {
    uint32_t struct_size;
    uint32_t parameter_id;
    San14ScenarioUnit unit;
    int32_t minimum, maximum;
    int32_t defaults[5];
    San14StringView label;       /* empty keeps native text */
    San14StringView description; /* empty keeps native text */
} San14ScenarioParameterSpec;

/* Custom boolean rows participate in native settings preset slots. Keys are
   local to the owning mod. 0 selects false_label; 1 selects true_label.
   Binding NONE leaves behavior to the mod (read_bool); built-in bindings are
   semantic capabilities, never addresses. Query binding support by configure.
   Presets are saved in Base sidecars; native game files are not extended. */
typedef enum San14ScenarioBoolBinding {
    SAN14_SCENARIO_BOOL_UNBOUND = 0,
    SAN14_SCENARIO_BOOL_MANUAL_TACTICS = 1
} San14ScenarioBoolBinding;
typedef struct San14ScenarioBoolSpec {
    uint32_t struct_size;
    San14StringView key;
    uint32_t after_parameter; /* existing native parameter, 0..60 */
    San14StringView label, description, false_label, true_label;
    int32_t defaults[5];
    San14ScenarioBoolBinding binding;
} San14ScenarioBoolSpec;
typedef enum San14ScenarioCustomKind {
    SAN14_SCENARIO_CUSTOM_CITY_CAP = 1,
    SAN14_SCENARIO_CUSTOM_OFFICER_STAT_CAP = 2,
    SAN14_SCENARIO_CUSTOM_FORMATION = 3,
    SAN14_SCENARIO_CUSTOM_MILITARY_INTEL_CAP = 4,
    SAN14_SCENARIO_CUSTOM_OFFICER_HIRE_CAP = 5,
} San14ScenarioCustomKind;

/* Military intel group 1..4: no title/inspector; governor/colonel;
   general/grand marshal; duke/king/emperor and equivalent special titles.
   Hire group 1..7: highest titles, grand marshal, general, colonel,
   governor, inspector, no title. field=0 for both kinds. */
typedef struct San14ScenarioCustomInfo {
    uint32_t struct_size;
    int32_t minimum, maximum, standard_value;
} San14ScenarioCustomInfo;

/* Group values for SAN14_SCENARIO_CUSTOM_CITY_CAP.
   Tribal strongholds (slot 47..51) are managed strictly by SAN14_SCENARIO_CITY_TRIBAL
   and are never modified by tiers 1..4 (small..mega). */
typedef enum San14ScenarioCitySize {
    SAN14_SCENARIO_CITY_SMALL = 1,
    SAN14_SCENARIO_CITY_MEDIUM = 2,
    SAN14_SCENARIO_CITY_LARGE = 3,
    SAN14_SCENARIO_CITY_MEGA = 4,
    SAN14_SCENARIO_CITY_TRIBAL = 5,
} San14ScenarioCitySize;

typedef enum San14ScenarioCityResource {
    SAN14_SCENARIO_CITY_GOLD = 0,
    SAN14_SCENARIO_CITY_FOOD = 1,
    SAN14_SCENARIO_CITY_SOLDIERS = 2,
    SAN14_SCENARIO_CITY_DURABILITY = 3,
} San14ScenarioCityResource;

typedef struct San14ScenarioCustomSpec {
    uint32_t struct_size;
    San14StringView key;
    San14StringView label;
    San14StringView help;
    uint32_t kind;   /* San14ScenarioCustomKind */
    uint32_t group;  /* CITY_CAP: 1..5 (Small..Tribal); OFFICER_STAT_CAP: 0; FORMATION: 1..14 */
    uint32_t field;  /* CITY_CAP: 0..3; OFFICER_STAT_CAP: 0; FORMATION: 0..4 */
    int32_t minimum;
    int32_t maximum;
    int32_t step;
    int32_t default_value;
} San14ScenarioCustomSpec;

typedef struct San14ScenarioApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *query)(void*, uint32_t, San14ScenarioParameterInfo*);
    /* Replaces this owner's complete configuration. count=0 releases it.
       Conflicting owners return BUSY; failure preserves the old batch.
       No save-format change: coefficient UI values use native percentage
       storage; while registered the adapter restores exact integer effects.
       Disabling restores native interpretation, including percentage scaling
       of policy bonuses. Saved settings are not reverted. */
    San14Result (SAN14_MOD_CALL *configure)(void*, const San14ScenarioParameterSpec*, uint32_t count);
    /* Atomic replacement of this mod's boolean rows; at most 16 in total.
       BUSY while a settings dialog exists, including removal. */
    San14Result (SAN14_MOD_CALL *configure_bools)(void*, const San14ScenarioBoolSpec*, uint32_t count);
    /* Read confirmed current value (defaults resolve against current difficulty).
       An unconfirmed settings dialog never changes gameplay behavior. */
    San14Result (SAN14_MOD_CALL *read_bool)(void*, San14StringView key, int32_t* value);
    /* Atomic replacement of this mod's runtime custom cap/formation specs.
       Each spec holds an independent confirmed integer value applied directly
       to gameplay (and immediately via write_custom) without injecting extra tabs
       into the native Scenario Settings dialog. */
    San14Result (SAN14_MOD_CALL *configure_custom)(void*, const San14ScenarioCustomSpec*, uint32_t count);
    /* Read confirmed current value of a registered custom gameplay parameter. */
    San14Result (SAN14_MOD_CALL *read_custom)(void*, San14StringView key, int32_t* value);
    /* Write a custom gameplay parameter and re-apply active custom caps/formations
       immediately if in-game. */
    San14Result (SAN14_MOD_CALL *write_custom)(void*, San14StringView key, int32_t value);
    /* Optional tail. Query a semantic parameter before configuring new kinds.
       UNSUPPORTED means this build has no verified implementation. */
    San14Result (SAN14_MOD_CALL *query_custom)(void*, uint32_t kind, uint32_t group,
                                             uint32_t field, San14ScenarioCustomInfo*);
} San14ScenarioApi;
#endif
