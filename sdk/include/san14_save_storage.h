#ifndef SAN14_MOD_SDK_SAVE_STORAGE_H
#define SAN14_MOD_SDK_SAVE_STORAGE_H
#include "san14_events.h"

/* Optional service. The existing storage.read/write_atomic accept "save" and
   "playthrough" when this service is present. Both scopes follow save rollback.
   Writes change the live session; native save success commits a captured copy.
   Keys are logical names, never filesystem paths. Payloads may contain any
   binary format. No arbitrary files or OS writes are intercepted.
   Call context first: it admits scoped read/write/erase for that Mod's returned
   revision. A later session switch returns STALE_GENERATION; re-query and reload
   the cache before writing again. Context itself does not persist data. */
typedef struct San14SaveStorageInfo {
    uint32_t struct_size;
    uint32_t state;
    uint64_t revision; /* changes on session switches, distinct from world generation */
    char playthrough_id[33]; /* opaque persisted ID; empty when unavailable */
    char save_basename[64]; /* no account identifier or directory */
    uint32_t max_value_bytes;
    uint32_t max_session_bytes;
    uint32_t reserved;
} San14SaveStorageInfo;

/* Explicit editing of this Mod's attached records. Never edits native game
   bytes. The callback receives only a temporary, owner-scoped record store;
   it must not call other Host services or retain its pointers. CURRENT needs
   an existing current save; ALL includes manual/automatic saves in the active
   account. A failed preflight writes nothing; publication failure attempts
   rollback and reports its outcome. Original attachments are backed up. */
enum { SAN14_SAVED_EDIT_CURRENT=1, SAN14_SAVED_EDIT_ALL=2 };
typedef struct San14SavedDataAccess {
    uint32_t struct_size;
    void* context;
    San14Result (SAN14_MOD_CALL *read)(void*, San14StringView scope, San14StringView key, void*, uint32_t capacity, uint32_t* required);
    San14Result (SAN14_MOD_CALL *write)(void*, San14StringView scope, San14StringView key, const void*, uint32_t size);
} San14SavedDataAccess;
typedef San14Result (SAN14_MOD_CALL *San14SavedDataTransform)(void* user, const San14SavedDataAccess* data);
typedef struct San14SavedEditResult {
    uint32_t struct_size;
    uint32_t matched_files;
    uint32_t updated_files;
    uint32_t rollback_failed;
} San14SavedEditResult;
typedef struct San14SaveStorageApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *context)(void* host_context, San14SaveStorageInfo* out_info);
    San14Result (SAN14_MOD_CALL *erase)(void* host_context, San14StringView scope, San14StringView key);
    /* Optional tail: UI thread; storage.saved-edit plus each accessed scope's
       permission. Call context first; expected revision must still match. */
    San14Result (SAN14_MOD_CALL *edit_saved)(void*, uint32_t target, uint64_t expected_revision,
        San14SavedDataTransform, void* user, San14SavedEditResult* result);
} San14SaveStorageApi;
#endif
