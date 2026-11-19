#ifndef SAN14_MOD_SDK_CLIPBOARD_H
#define SAN14_MOD_SDK_CLIPBOARD_H
#include "san14_types.h"
/* Optional write-only service. UI thread, ui.panel permission, loaded owner.
 * Copies at most 64 KiB of valid UTF-8 (no embedded NUL) to Windows Unicode
 * clipboard. Returns BUSY if occupied. No clipboard reading is exposed. */
typedef struct San14ClipboardApi {
    uint32_t struct_size;
    San14Result (SAN14_MOD_CALL *copy_text)(void* host_context, San14StringView text);
} San14ClipboardApi;
#endif
