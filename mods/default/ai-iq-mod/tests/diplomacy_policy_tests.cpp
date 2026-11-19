#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <array>
#include <cassert>
#include <iostream>
#include <vector>

#include "../src/diplomacy_policy.h"

namespace dip_ns = companion_mod::diplomacy_policy;

int main() {
    dip_ns::State st;
    st.set_enabled(true);
    auto policy = dip_ns::make_policy(st);

    std::array<uint8_t, SAN14_DIPLOMACY_MATRIX_STRIDE * SAN14_DIPLOMACY_MATRIX_STRIDE> rel{};
    std::array<uint8_t, SAN14_DIPLOMACY_MATRIX_STRIDE * SAN14_DIPLOMACY_MATRIX_STRIDE> treaties{};

    // Forces 1 and 2 have good relations (>= 45) and dislike strong/coalition Force 3.
    // Forces 4 and 3 dislike each other mutually.
    auto set_rel = [&](uint32_t a, uint32_t b, uint8_t v) {
        rel[a * SAN14_DIPLOMACY_MATRIX_STRIDE + b] = v;
        rel[b * SAN14_DIPLOMACY_MATRIX_STRIDE + a] = v;
    };
    set_rel(1, 2, 50);
    set_rel(1, 3, 5);
    set_rel(2, 3, 5);
    set_rel(1, 4, 55);
    set_rel(2, 4, 55);

    std::vector<San14DiplomacyForce> forces = {
        {sizeof(San14DiplomacyForce), 1, 10, 3, 30000, 15, 4000, 1, 0, {}},
        {sizeof(San14DiplomacyForce), 2, 20, 3, 28000, 14, 3800, 1, 0, {}},
        {sizeof(San14DiplomacyForce), 3, 30, 10, 120000, 50, 18000, 1, 0, {}},
    };

    San14DiplomacySnapshot snap{};
    snap.struct_size = sizeof(snap);
    snap.year = 207;
    snap.month = 3;
    snap.day = 1;
    snap.total_days = 1000;
    snap.elapsed_days = 100;
    snap.coalition_target_force_id = 3;
    snap.forces = forces.data();
    snap.force_count = static_cast<uint32_t>(forces.size());
    snap.relations = rel.data();
    snap.treaties = treaties.data();
    snap.matrix_stride = SAN14_DIPLOMACY_MATRIX_STRIDE;

    policy.on_tick(policy.plugin_context, &snap);

    // 1 and 2 share common main enemy 3; 1 and 3 are mutual enemies.
    int32_t val = -1;
    assert(policy.on_relation_query(policy.plugin_context, &snap, 1, 2, 1, &val) == SAN14_OK && val == 3);
    assert(policy.on_duration_query(policy.plugin_context, &snap, 1, 2, 6, &val) == SAN14_OK && val == 12);
    assert(policy.on_cost_query(policy.plugin_context, &snap, 1, 2, 1000, &val) == SAN14_OK && val == 700);
    int32_t block = 0;
    assert(policy.on_block_annul_query(policy.plugin_context, &snap, 1, 2, &block) == SAN14_OK && block == 1);

    // Mutual enemy 1 vs 3 blocks alliance acceptance and adds +1000 cost.
    assert(policy.on_acceptance_query(policy.plugin_context, &snap, 1, 3, 1, &val) == SAN14_OK && val == 0);
    assert(policy.on_cost_query(policy.plugin_context, &snap, 1, 3, 1000, &val) == SAN14_OK && val == 2000);

    // Common enemy 20% roll hits on some day and enforces 90-day cooldown on failure.
    bool saw_accept = false;
    bool saw_cooldown = false;
    for (int32_t d = 100; d < 400; ++d) {
        st.set_enabled(false);
        st.set_enabled(true);
        snap.elapsed_days = d;
        snap.total_days = 1000 + d;
        int32_t acc = 0;
        if (policy.on_acceptance_query(policy.plugin_context, &snap, 1, 2, 0, &acc) == SAN14_OK && acc == 1) {
            saw_accept = true;
        } else {
            // Immediate retry inside 90 days must stay rejected regardless of roll hash.
            snap.total_days += 1;
            snap.elapsed_days += 1;
            assert(policy.on_acceptance_query(policy.plugin_context, &snap, 1, 2, 0, &acc) != SAN14_OK);
            saw_cooldown = true;
        }
        if (saw_accept && saw_cooldown) break;
    }
    assert(saw_accept && saw_cooldown);

    std::cout << "diplomacy_policy_tests OK\n";
    return 0;
}
