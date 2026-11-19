#ifndef SAN14_COMPANION_DIPLOMACY_POLICY_H
#define SAN14_COMPANION_DIPLOMACY_POLICY_H

#include <algorithm>
#include <array>
#include <cstdint>
#include <mutex>

#include "san14_diplomacy.h"

namespace companion_mod {
namespace diplomacy_policy {

constexpr int32_t TARGET_LOCK_DAYS = 120;
constexpr int32_t FAILURE_COOLDOWN_DAYS = 90;
constexpr int32_t COALITION_WEIGHT = 160;
constexpr int32_t MUTUAL_ENEMY_COST_PENALTY = 1000;

struct State {
    std::mutex mutex;
    bool enabled = false;
    int32_t last_elapsed_days = -1;
    std::array<uint32_t, SAN14_DIPLOMACY_MATRIX_STRIDE> main_enemy{};
    std::array<int32_t, SAN14_DIPLOMACY_MATRIX_STRIDE> lock_until{};
    std::array<int32_t, SAN14_DIPLOMACY_MATRIX_STRIDE * SAN14_DIPLOMACY_MATRIX_STRIDE> failure_until{};

    uint64_t common_enemy_accepts = 0;
    uint64_t mutual_enemy_blocks = 0;
    uint64_t duration_overrides = 0;
    uint64_t relation_adjustments = 0;
    uint64_t cost_adjustments = 0;
    uint64_t annul_blocks = 0;

    void clear_runtime() {
        main_enemy.fill(0);
        lock_until.fill(0);
        failure_until.fill(0);
        last_elapsed_days = -1;
    }

    void set_enabled(bool value) {
        std::lock_guard<std::mutex> lock(mutex);
        enabled = value;
        if (!value) clear_runtime();
    }
};

inline bool valid_force(uint32_t force_id) {
    return force_id >= 1 && force_id < SAN14_DIPLOMACY_MATRIX_STRIDE;
}

inline const San14DiplomacyForce* find_force(const San14DiplomacySnapshot* snap, uint32_t force_id) {
    if (!snap || !snap->forces || !valid_force(force_id)) return nullptr;
    for (uint32_t i = 0; i < snap->force_count; ++i) {
        if (snap->forces[i].force_id == force_id) return &snap->forces[i];
    }
    return nullptr;
}

inline int32_t force_power(const San14DiplomacyForce& f) {
    if (f.diplomacy_power > 0) return f.diplomacy_power;
    return (std::max)(1, int32_t(f.city_count) * 1200 + f.troop_count / 100 + int32_t(f.officer_count) * 40);
}

inline uint8_t get_relation(const San14DiplomacySnapshot* snap, uint32_t a, uint32_t b) {
    if (!snap || !snap->relations || !valid_force(a) || !valid_force(b)) return 0;
    const uint32_t stride = snap->matrix_stride ? snap->matrix_stride : SAN14_DIPLOMACY_MATRIX_STRIDE;
    if (a >= stride || b >= stride) return 0;
    return snap->relations[a * stride + b];
}

inline bool has_treaty(const San14DiplomacySnapshot* snap, uint32_t a, uint32_t b) {
    if (!snap || !snap->treaties || !valid_force(a) || !valid_force(b)) return false;
    const uint32_t stride = snap->matrix_stride ? snap->matrix_stride : SAN14_DIPLOMACY_MATRIX_STRIDE;
    if (a >= stride || b >= stride) return false;
    return (snap->treaties[a * stride + b] & 0x7fu) != 0;
}

inline uint32_t deterministic_percent(int32_t total_days, uint32_t a, uint32_t b) {
    const uint32_t lo = (std::min)(a, b);
    const uint32_t hi = (std::max)(a, b);
    uint32_t h = static_cast<uint32_t>(total_days) * 2654435761u;
    h ^= lo * 2246822519u;
    h ^= hi * 3266489917u;
    h ^= h >> 13;
    h *= 1274126177u;
    h ^= h >> 16;
    return h % 100u;
}

inline void recompute_locked(State& st, const San14DiplomacySnapshot* snap, bool force_refresh) {
    if (!snap || !snap->forces) return;
    if (!force_refresh && st.last_elapsed_days == snap->elapsed_days) return;
    st.last_elapsed_days = snap->elapsed_days;

    for (uint32_t i = 0; i < snap->force_count; ++i) {
        const auto& fa = snap->forces[i];
        const uint32_t a = fa.force_id;
        if (!valid_force(a) || !fa.alive) {
            if (valid_force(a)) {
                st.main_enemy[a] = 0;
                st.lock_until[a] = 0;
            }
            continue;
        }

        const uint32_t current = st.main_enemy[a];
        if (!force_refresh && current != 0 && snap->elapsed_days < st.lock_until[a]) {
            const auto* fc = find_force(snap, current);
            if (fc && fc->alive && !has_treaty(snap, a, current)) continue;
        }

        const int32_t pa = (std::max)(1, force_power(fa));
        uint32_t best_id = 0;
        int64_t best_score = -1;

        for (uint32_t j = 0; j < snap->force_count; ++j) {
            const auto& fb = snap->forces[j];
            const uint32_t b = fb.force_id;
            if (!valid_force(b) || !fb.alive || b == a || has_treaty(snap, a, b)) continue;
            const int32_t pb = (std::max)(1, force_power(fb));
            const int32_t rel = get_relation(snap, a, b);
            const int64_t hostility = int64_t((std::max)(0, 100 - rel)) * 3;
            const int64_t threat = (int64_t(pb) * 100) / pa;
            const int64_t coalition = (snap->coalition_target_force_id == b) ? COALITION_WEIGHT : 0;
            const int64_t score = hostility + threat + coalition;
            if (score > best_score || (score == best_score && b < best_id)) {
                best_score = score;
                best_id = b;
            }
        }
        st.main_enemy[a] = best_id;
        st.lock_until[a] = best_id ? (snap->elapsed_days + TARGET_LOCK_DAYS) : 0;
    }
}

enum class PairRelation { Neutral, CommonEnemy, MutualEnemy };

inline PairRelation classify_pair_locked(const State& st, uint32_t a, uint32_t b) {
    if (!valid_force(a) || !valid_force(b) || a == b) return PairRelation::Neutral;
    const uint32_t ea = st.main_enemy[a];
    const uint32_t eb = st.main_enemy[b];
    if (ea == b || eb == a) return PairRelation::MutualEnemy;
    if (ea != 0 && ea == eb) return PairRelation::CommonEnemy;
    return PairRelation::Neutral;
}

inline void SAN14_MOD_CALL on_reset(void* plugin_context) {
    if (!plugin_context) return;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    st->clear_runtime();
}

inline void SAN14_MOD_CALL on_tick(void* plugin_context, const San14DiplomacySnapshot* snapshot) {
    if (!plugin_context || !snapshot) return;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->enabled) return;
    recompute_locked(*st, snapshot, false);
}

inline San14Result SAN14_MOD_CALL on_relation_query(
    void* plugin_context, const San14DiplomacySnapshot* snapshot,
    uint32_t force_a, uint32_t force_b, int32_t native_value, int32_t* out_value
) {
    if (!plugin_context || !snapshot || !out_value) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->enabled) return SAN14_E_UNSUPPORTED;
    recompute_locked(*st, snapshot, false);
    if (classify_pair_locked(*st, force_a, force_b) == PairRelation::CommonEnemy && native_value < 3) {
        *out_value = 3;
        ++st->relation_adjustments;
        return SAN14_OK;
    }
    return SAN14_E_UNSUPPORTED;
}

inline San14Result SAN14_MOD_CALL on_acceptance_query(
    void* plugin_context, const San14DiplomacySnapshot* snapshot,
    uint32_t force_a, uint32_t force_b, int32_t native_value, int32_t* out_value
) {
    if (!plugin_context || !snapshot || !out_value) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->enabled) return SAN14_E_UNSUPPORTED;
    recompute_locked(*st, snapshot, false);
    const PairRelation rel_type = classify_pair_locked(*st, force_a, force_b);
    if (rel_type == PairRelation::MutualEnemy) {
        *out_value = 0;
        ++st->mutual_enemy_blocks;
        return SAN14_OK;
    }
    if (rel_type != PairRelation::CommonEnemy) return SAN14_E_UNSUPPORTED;

    const size_t idx_ab = force_a * SAN14_DIPLOMACY_MATRIX_STRIDE + force_b;
    const size_t idx_ba = force_b * SAN14_DIPLOMACY_MATRIX_STRIDE + force_a;
    if (native_value > 0) {
        st->failure_until[idx_ab] = 0;
        st->failure_until[idx_ba] = 0;
        *out_value = native_value;
        return SAN14_OK;
    }
    if (snapshot->elapsed_days < st->failure_until[idx_ab]) return SAN14_E_UNSUPPORTED;
    if (get_relation(snapshot, force_a, force_b) < 20) return SAN14_E_UNSUPPORTED;

    if (deterministic_percent(snapshot->total_days, force_a, force_b) < 20u) {
        st->failure_until[idx_ab] = 0;
        st->failure_until[idx_ba] = 0;
        *out_value = 1;
        ++st->common_enemy_accepts;
        return SAN14_OK;
    }
    st->failure_until[idx_ab] = snapshot->elapsed_days + FAILURE_COOLDOWN_DAYS;
    st->failure_until[idx_ba] = snapshot->elapsed_days + FAILURE_COOLDOWN_DAYS;
    return SAN14_E_UNSUPPORTED;
}

inline San14Result SAN14_MOD_CALL on_cost_query(
    void* plugin_context, const San14DiplomacySnapshot* snapshot,
    uint32_t force_a, uint32_t force_b, int32_t native_value, int32_t* out_value
) {
    if (!plugin_context || !snapshot || !out_value) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->enabled) return SAN14_E_UNSUPPORTED;
    recompute_locked(*st, snapshot, false);
    const PairRelation rel_type = classify_pair_locked(*st, force_a, force_b);
    if (rel_type == PairRelation::MutualEnemy) {
        *out_value = native_value + MUTUAL_ENEMY_COST_PENALTY;
        ++st->cost_adjustments;
        return SAN14_OK;
    }
    if (rel_type == PairRelation::CommonEnemy) {
        *out_value = (std::max)(0, (native_value * 70) / 100);
        ++st->cost_adjustments;
        return SAN14_OK;
    }
    return SAN14_E_UNSUPPORTED;
}

inline San14Result SAN14_MOD_CALL on_duration_query(
    void* plugin_context, const San14DiplomacySnapshot* snapshot,
    uint32_t force_a, uint32_t force_b, int32_t native_value, int32_t* out_value
) {
    if (!plugin_context || !snapshot || !out_value) return SAN14_E_INVALID_ARGUMENT;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->enabled) return SAN14_E_UNSUPPORTED;
    recompute_locked(*st, snapshot, false);
    if (classify_pair_locked(*st, force_a, force_b) == PairRelation::CommonEnemy && native_value == 6) {
        *out_value = 12;
        ++st->duration_overrides;
        return SAN14_OK;
    }
    return SAN14_E_UNSUPPORTED;
}

inline San14Result SAN14_MOD_CALL on_block_annul_query(
    void* plugin_context, const San14DiplomacySnapshot* snapshot,
    uint32_t force_a, uint32_t force_b, int32_t* out_block
) {
    if (!plugin_context || !snapshot || !out_block) return SAN14_E_INVALID_ARGUMENT;
    *out_block = 0;
    auto* st = static_cast<State*>(plugin_context);
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->enabled) return SAN14_E_UNSUPPORTED;
    recompute_locked(*st, snapshot, false);
    if (classify_pair_locked(*st, force_a, force_b) == PairRelation::CommonEnemy) {
        *out_block = 1;
        ++st->annul_blocks;
        return SAN14_OK;
    }
    return SAN14_E_UNSUPPORTED;
}

inline San14DiplomacyPolicy make_policy(State& state) {
    San14DiplomacyPolicy p{};
    p.struct_size = sizeof(p);
    p.plugin_context = &state;
    p.on_reset = &on_reset;
    p.on_tick = &on_tick;
    p.on_relation_query = &on_relation_query;
    p.on_acceptance_query = &on_acceptance_query;
    p.on_cost_query = &on_cost_query;
    p.on_duration_query = &on_duration_query;
    p.on_block_annul_query = &on_block_annul_query;
    return p;
}

}  // namespace diplomacy_policy
}  // namespace companion_mod

#endif
