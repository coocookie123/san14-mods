#ifndef SAN14_MOD_SDK_EVENTS_H
#define SAN14_MOD_SDK_EVENTS_H

#include "san14_types.h"

typedef enum San14EventId {
    SAN14_EVENT_GAME_READY = 0x0001,
    SAN14_EVENT_GAME_EXITING = 0x0002,
    SAN14_EVENT_SAVE_LOADING = 0x0101,
    SAN14_EVENT_SAVE_LOADED = 0x0102,
    SAN14_EVENT_SAVE_UNLOADED = 0x0103,
    SAN14_EVENT_SAVE_SAVING = 0x0104,
    SAN14_EVENT_SAVE_SAVED = 0x0105,
    SAN14_EVENT_SAVE_FAILED = 0x0106,
    SAN14_EVENT_STORAGE_CHANGED = 0x0107,
    SAN14_EVENT_NEW_GAME = 0x0108,
    SAN14_EVENT_TURN_STARTED = 0x0201,
    SAN14_EVENT_TURN_ENDED = 0x0202,
    SAN14_EVENT_OFFICER_DETAIL_OPENED = 0x1001,
    SAN14_EVENT_OFFICER_DETAIL_CLOSED = 0x1002,
    SAN14_EVENT_OFFICER_CHANGED = 0x1003
} San14EventId;

typedef enum San14StorageState {
    SAN14_STORAGE_UNAVAILABLE = 0,
    SAN14_STORAGE_READY = 1,
    SAN14_STORAGE_LOADING = 2,
    SAN14_STORAGE_ERROR = 3
} San14StorageState;

/* SAVE_*, STORAGE_CHANGED and NEW_GAME payload. Delivered sequentially by the
   Host after the native operation is observed (either on the UI pump or
   serialized immediately before a host-driven data refresh); SAVE_SAVING is a
   notification, not a synchronous opportunity to change the captured save. No
   pointer is retained. revision identifies the Host storage state, not a slot
   or world generation. NEW_GAME follows an observed new-campaign scenario
   entry and its first ready map, after STORAGE_CHANGED and before that pump's
   UI_IDLE (and before an immediate roster refresh). It is not replayed on
   plugin enable, save loads with missing attachments, or late map attachment.
   Inside these callbacks, query save_storage->context and require a matching
   ready revision before reading/writing attachments or capturing baselines;
   use scheduler.dispatch(SAN14_THREAD_UI, ...) if UI panel updates are needed. */
typedef struct San14StorageEventInfo {
    uint32_t struct_size;
    uint32_t state;
    uint64_t revision;
    int32_t result;
    uint32_t reserved;
} San14StorageEventInfo;

typedef struct San14EventView {
    uint32_t struct_size;
    uint32_t event_id;
    uint64_t generation;
    const void* payload;
    uint32_t payload_size;
    uint32_t reserved;
} San14EventView;

typedef void (SAN14_MOD_CALL *San14EventCallback)(
    void* plugin_context,
    const San14EventView* event_view
);

typedef struct San14EventApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *subscribe)(
        void* host_context,
        uint32_t event_id,
        int32_t priority,
        San14EventCallback callback,
        void* plugin_context,
        San14Subscription* out_subscription
    );
    San14Result (SAN14_MOD_CALL *unsubscribe)(
        void* host_context,
        San14Subscription subscription
    );
} San14EventApi;

#endif
