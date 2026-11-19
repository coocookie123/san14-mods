#ifndef SAN14_COMPANION_PERSISTENT_AREA_POLICY_H
#define SAN14_COMPANION_PERSISTENT_AREA_POLICY_H

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "san14_area.h"

namespace companion_mod {
namespace persistent_area_policy {

struct SuspendedAreaEntry {
    uint32_t area_id = 0;
    uint32_t officer_id = 0;
    uint8_t dev_type = 0;
    uint8_t aux_mode = 0;
    uint32_t since_date_key = 0;
};

struct State {
    std::mutex mutex;
    bool enabled = false;
    bool dirty = false;
    uint32_t last_date_key = 0;
    uint32_t restored_count = 0;
    std::unordered_map<uint32_t, uint32_t> active_player_officers;
    std::unordered_map<uint32_t, std::pair<uint8_t, uint8_t>> last_modes;
    std::unordered_map<uint32_t, SuspendedAreaEntry> suspended;

    void set_enabled(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        enabled = value;
        if (!value) {
            active_player_officers.clear();
            last_modes.clear();
        }
    }

    void reset_for_new_playthrough() {
        std::lock_guard<std::mutex> lock(mutex);
        active_player_officers.clear();
        last_modes.clear();
        suspended.clear();
        last_date_key = 0;
        dirty = false;
    }

    bool consume_dirty(std::vector<uint8_t>& out_blob) {
        std::lock_guard<std::mutex> lock(mutex);
        if (!dirty) return false;
        out_blob = serialize_locked();
        dirty = false;
        return true;
    }

    std::vector<uint8_t> snapshot_blob() {
        std::lock_guard<std::mutex> lock(mutex);
        return serialize_locked();
    }

    bool load_blob(const uint8_t* data, size_t size) {
        std::lock_guard<std::mutex> lock(mutex);
        suspended.clear();
        active_player_officers.clear();
        last_modes.clear();
        dirty = false;
        if (!data || size == 0) return true;
        if (size < 12) return false;
        uint32_t magic = 0, version = 0, count = 0;
        std::memcpy(&magic, data, 4);
        std::memcpy(&version, data + 4, 4);
        std::memcpy(&count, data + 8, 4);
        if (magic != 0x41343153u || version != 1u) return false;  // 'S14A'
        if (size < 12 + size_t(count) * 16) return false;
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* rec = data + 12 + size_t(i) * 16;
            SuspendedAreaEntry e{};
            std::memcpy(&e.area_id, rec, 4);
            std::memcpy(&e.officer_id, rec + 4, 4);
            std::memcpy(&e.since_date_key, rec + 8, 4);
            e.dev_type = rec[12];
            e.aux_mode = rec[13];
            if (e.area_id >= 1 && e.area_id <= SAN14_AREA_MAX_ID && e.officer_id > 0) {
                suspended[e.area_id] = e;
            }
        }
        return true;
    }

private:
    std::vector<uint8_t> serialize_locked() const {
        std::vector<uint8_t> blob;
        const uint32_t magic = 0x41343153u;
        const uint32_t version = 1u;
        const uint32_t count = static_cast<uint32_t>(suspended.size());
        blob.resize(12 + size_t(count) * 16, 0);
        std::memcpy(blob.data(), &magic, 4);
        std::memcpy(blob.data() + 4, &version, 4);
        std::memcpy(blob.data() + 8, &count, 4);

        std::vector<SuspendedAreaEntry> sorted;
        sorted.reserve(suspended.size());
        for (const auto& kv : suspended) sorted.push_back(kv.second);
        std::sort(sorted.begin(), sorted.end(), [](const SuspendedAreaEntry& a, const SuspendedAreaEntry& b) {
            return a.area_id < b.area_id;
        });
        for (size_t i = 0; i < sorted.size(); ++i) {
            uint8_t* rec = blob.data() + 12 + i * 16;
            std::memcpy(rec, &sorted[i].area_id, 4);
            std::memcpy(rec + 4, &sorted[i].officer_id, 4);
            std::memcpy(rec + 8, &sorted[i].since_date_key, 4);
            rec[12] = sorted[i].dev_type;
            rec[13] = sorted[i].aux_mode;
        }
        return blob;
    }
};

inline const San14AreaSnapshot* find_area(const San14AreaSyncContext* ctx, uint32_t area_id) {
    if (!ctx || !ctx->areas || area_id == 0) return nullptr;
    for (uint32_t i = 0; i < ctx->area_count; ++i) {
        if (ctx->areas[i].area_id == area_id) return &ctx->areas[i];
    }
    return nullptr;
}

inline bool query_officer(const San14AreaSyncContext* ctx, uint32_t officer_id, San14AreaOfficerState& out) {
    out = {};
    out.struct_size = sizeof(out);
    if (!ctx || !ctx->lookup_officer || officer_id == 0) return false;
    return ctx->lookup_officer(ctx->lookup_context, officer_id, &out) == SAN14_OK && out.is_valid != 0;
}

inline void handle_reverse_event(State& st, const San14AreaSyncContext* ctx) {
    const uint32_t aid = ctx->event_area_id;
    if (aid < 1 || aid > SAN14_AREA_MAX_ID) return;
    const San14AreaSnapshot* snap = find_area(ctx, aid);
    const uint8_t dev_type = snap
        ? static_cast<uint8_t>(snap->dev_type)
        : (st.last_modes.count(aid) ? st.last_modes[aid].first : uint8_t(0));
    const uint8_t aux_mode = snap
        ? static_cast<uint8_t>(snap->aux_mode)
        : (st.last_modes.count(aid) ? st.last_modes[aid].second : uint8_t(0));

    if (ctx->event_new_officer_id != 0) {
        if (st.suspended.erase(aid) > 0) st.dirty = true;
        if (!snap || snap->force_id == ctx->player_force_id) {
            st.active_player_officers[aid] = ctx->event_new_officer_id;
            st.last_modes[aid] = {dev_type, aux_mode};
        }
        return;
    }

    if (ctx->event_old_officer_id == 0) return;
    const bool was_player = st.active_player_officers.count(aid) > 0 ||
        (snap && snap->force_id == ctx->player_force_id);
    if (!was_player || ctx->player_force_id == 0) return;

    San14AreaOfficerState off{};
    const bool ok = query_officer(ctx, ctx->event_old_officer_id, off);
    const bool lost_area = !snap || snap->force_id != ctx->player_force_id;
    if (ok && (off.is_captive || lost_area)) {
        st.suspended[aid] = {aid, ctx->event_old_officer_id, dev_type, aux_mode, ctx->date_key};
        st.dirty = true;
    }
    st.active_player_officers.erase(aid);
}

inline San14Result SAN14_MOD_CALL on_sync(
    void* plugin_context,
    const San14AreaSyncContext* ctx,
    San14AreaAssignAction* out_actions,
    uint32_t max_actions,
    uint32_t* out_count
) {
    if (!plugin_context || !ctx || !out_count) return SAN14_E_INVALID_ARGUMENT;
    *out_count = 0;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->enabled) return SAN14_OK;

    if (ctx->phase == SAN14_AREA_SYNC_WORLD_RESET) {
        st->active_player_officers.clear();
        st->last_modes.clear();
        return SAN14_OK;
    }

    if (ctx->date_key > 0) {
        st->last_date_key = ctx->date_key;
        for (auto it = st->suspended.begin(); it != st->suspended.end();) {
            if (it->second.since_date_key > ctx->date_key) {
                it = st->suspended.erase(it);
                st->dirty = true;
            } else {
                ++it;
            }
        }
    }

    if (ctx->phase == SAN14_AREA_SYNC_REVERSE_UPDATE) {
        handle_reverse_event(*st, ctx);
    }

    const uint32_t player = ctx->player_force_id;
    if (player == 0 || !ctx->areas) return SAN14_OK;

    for (uint32_t i = 0; i < ctx->area_count; ++i) {
        const auto& area = ctx->areas[i];
        const uint32_t aid = area.area_id;
        if (aid < 1 || aid > SAN14_AREA_MAX_ID) continue;

        if (area.force_id == player) {
            const auto prev_modes = st->last_modes.count(aid)
                ? st->last_modes[aid]
                : std::make_pair(static_cast<uint8_t>(area.dev_type), static_cast<uint8_t>(area.aux_mode));
            st->last_modes[aid] = {static_cast<uint8_t>(area.dev_type), static_cast<uint8_t>(area.aux_mode)};

            if (area.officer_id != 0) {
                st->active_player_officers[aid] = area.officer_id;
                if (st->suspended.erase(aid) > 0) st->dirty = true;
            } else if (st->active_player_officers.count(aid)) {
                const uint32_t old_off = st->active_player_officers[aid];
                San14AreaOfficerState off{};
                if (query_officer(ctx, old_off, off) && off.is_captive) {
                    st->suspended[aid] = {
                        aid, old_off, prev_modes.first, prev_modes.second, ctx->date_key
                    };
                    st->dirty = true;
                }
                st->active_player_officers.erase(aid);
            }
        } else if (st->active_player_officers.count(aid)) {
            const uint32_t old_off = st->active_player_officers[aid];
            const auto modes = st->last_modes.count(aid)
                ? st->last_modes[aid]
                : std::make_pair(static_cast<uint8_t>(area.dev_type), static_cast<uint8_t>(area.aux_mode));
            San14AreaOfficerState off{};
            if (query_officer(ctx, old_off, off) && (off.is_captive || off.is_serving || off.is_available)) {
                st->suspended[aid] = {
                    aid, old_off, modes.first, modes.second, ctx->date_key
                };
                st->dirty = true;
            }
            st->active_player_officers.erase(aid);
        }
    }

    uint32_t emitted = 0;
    for (auto it = st->suspended.begin(); it != st->suspended.end();) {
        const auto entry = it->second;
        const San14AreaSnapshot* area = find_area(ctx, entry.area_id);
        San14AreaOfficerState off{};
        const bool ok = query_officer(ctx, entry.officer_id, off);
        if (!area || !ok || (!off.is_serving && !off.is_captive) ||
            (!off.is_captive && off.force_id != 0 && off.force_id != player)) {
            it = st->suspended.erase(it);
            st->dirty = true;
            continue;
        }
        if (area->force_id == player && area->officer_id != 0) {
            it = st->suspended.erase(it);
            st->dirty = true;
            continue;
        }
        if (area->force_id == player && area->officer_id == 0 &&
            off.is_available && off.force_id == player) {
            if (out_actions && emitted < max_actions) {
                San14AreaAssignAction act{};
                act.area_id = entry.area_id;
                act.officer_id = entry.officer_id;
                act.restore_modes = 1;
                act.dev_type = entry.dev_type;
                act.aux_mode = entry.aux_mode;
                out_actions[emitted++] = act;
                ++st->restored_count;
                st->active_player_officers[entry.area_id] = entry.officer_id;
                it = st->suspended.erase(it);
                st->dirty = true;
                continue;
            }
        }
        ++it;
    }

    *out_count = emitted;
    return SAN14_OK;
}

inline San14AreaPolicy make_policy(State& state) {
    San14AreaPolicy p{};
    p.struct_size = sizeof(p);
    p.plugin_context = &state;
    p.on_sync = &on_sync;
    return p;
}

}  // namespace persistent_area_policy
}  // namespace companion_mod

#endif
