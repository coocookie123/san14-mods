#ifndef SAN14_MOD_SDK_ROSTER_TABS_H
#define SAN14_MOD_SDK_ROSTER_TABS_H
#include "san14_types.h"

/* Copied, numeric columns in the game's officer roster. No game object or
 * native descriptor escapes. At most 8 tabs globally, 8 columns per tab. */
typedef struct San14RosterColumn {
    uint32_t struct_size;
    San14StringView label;
    uint32_t flags;
    uint32_t reserved;
} San14RosterColumn;
#define SAN14_ROSTER_COLUMN_SIGNED 1u
/* Value is basis points: 10000 displays 100.00%. Missing can display "-". */
#define SAN14_ROSTER_COLUMN_PERCENT 2u
#define SAN14_ROSTER_COLUMN_DASH_IF_MISSING 4u
/* With PERCENT, round the basis-point value for display as e.g. 33.3%.
 * Numeric sorting still uses the original value. */
#define SAN14_ROSTER_COLUMN_PERCENT_ONE_DECIMAL 8u
typedef San14Result (SAN14_MOD_CALL *San14RosterTabRefresh)(void*);
typedef San14Result (SAN14_MOD_CALL *San14RosterTabValue)(void*,uint32_t officer_id,uint32_t column,int32_t*);
typedef struct San14RosterTabSpec {
    uint32_t struct_size;
    San14StringView title;
    const San14RosterColumn* columns;
    uint32_t column_count;
    uint32_t visible_mask;
    San14RosterTabRefresh refresh;
    San14RosterTabValue value;
    void* plugin_context;
} San14RosterTabSpec;
typedef struct San14RosterTabsApi {
    uint32_t struct_size;
    /* Registration (register_tab, set_visible_columns, unregister_tab) runs on
     * the UI thread for an active owner.
     *
     * refresh may execute on either the UI thread or a serialized host/game
     * roster thread when an officer table opens or refreshes; within refresh,
     * host read/snapshot/storage-context APIs (roster, save_storage, storage,
     * world, settings reads, scenario reads, memory) remain valid, whereas UI
     * control creation or tab registration requires the UI thread.
     *
     * value runs synchronously to populate the host cache and must only read
     * the plugin's refreshed cache. NOT_FOUND means unavailable (displayed as
     * invalid, sorted after numbers). All title/label San14StringView inputs
     * are UTF-8; the host validates, copies, and converts them to the game's
     * native UTF-16LE descriptor format. */
    San14Result (SAN14_MOD_CALL *register_tab)(void*,const San14RosterTabSpec*);
    San14Result (SAN14_MOD_CALL *set_visible_columns)(void*,uint32_t mask);
    San14Result (SAN14_MOD_CALL *unregister_tab)(void*);
    /* Optional, UI thread; one owner. Adds later deaths only to the in-game
     * all-force intelligence roster. Base saves an initial-dead baseline on
     * NEW_GAME; old saves without one start at first enable. Baseline follows
     * save rollback; corrupt data is never replaced. Reopen the list to apply.
     * No officer status changes and no selection/dispatch-list extension. */
    San14Result (SAN14_MOD_CALL *set_show_dead_officers)(void*,int32_t enabled);
    /* Optional named tabs. Empty key is the legacy/default tab. UI thread.
     * Keys: 1..64 ASCII letters/digits/._-. Cleanup removes all owner tabs. */
    San14Result (SAN14_MOD_CALL *register_named_tab)(void*,San14StringView key,const San14RosterTabSpec*);
    San14Result (SAN14_MOD_CALL *unregister_named_tab)(void*,San14StringView key);
} San14RosterTabsApi;
#define SAN14_ROSTER_TABS_API_V1_MIN_SIZE offsetof(San14RosterTabsApi, set_show_dead_officers)
#endif
