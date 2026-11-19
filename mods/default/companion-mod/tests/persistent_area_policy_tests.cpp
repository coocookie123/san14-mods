#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <cassert>
#include <cstring>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "../src/persistent_area_policy.h"

namespace policy_ns = companion_mod::persistent_area_policy;

namespace {
struct MockWorld {
    std::unordered_map<uint32_t, San14AreaOfficerState> officers;
    std::vector<San14AreaSnapshot> areas;
};

San14Result SAN14_MOD_CALL mock_lookup(void* ctx, uint32_t id, San14AreaOfficerState* out) {
    auto* w = static_cast<MockWorld*>(ctx);
    auto it = w->officers.find(id);
    if (it == w->officers.end()) return SAN14_E_NOT_FOUND;
    *out = it->second;
    out->struct_size = sizeof(*out);
    out->officer_id = id;
    return SAN14_OK;
}

San14AreaSyncContext make_ctx(MockWorld& w, San14AreaSyncPhase phase, uint32_t player, uint32_t date_key) {
    San14AreaSyncContext ctx{};
    ctx.struct_size = sizeof(ctx);
    ctx.phase = phase;
    ctx.player_force_id = player;
    ctx.date_key = date_key;
    ctx.areas = w.areas.empty() ? nullptr : w.areas.data();
    ctx.area_count = static_cast<uint32_t>(w.areas.size());
    ctx.lookup_context = &w;
    ctx.lookup_officer = &mock_lookup;
    return ctx;
}
}  // namespace

int main() {
    policy_ns::State st;
    st.set_enabled(true);
    auto policy = policy_ns::make_policy(st);

    MockWorld w;
    w.officers[42] = {sizeof(San14AreaOfficerState), 42, 2, 1, 7, 7, 1, 1, 1, 0, 1};
    w.areas.push_back({sizeof(San14AreaSnapshot), 12, 7, 1, 42, 100, 2, 3});  // aux=2, dev=3

    std::vector<San14AreaAssignAction> actions(16);
    uint32_t out_count = 0;

    // 1. Initial tick sees officer 42 serving in player area 12.
    auto ctx = make_ctx(w, SAN14_AREA_SYNC_PRE_TICK, 1, 2070101);
    assert(policy.on_sync(policy.plugin_context, &ctx, actions.data(), 16, &out_count) == SAN14_OK);
    assert(out_count == 0);

    // 2. Officer 42 captured and area 12 vacated -> suspends entry with modes (3, 2).
    w.officers[42].is_serving = 0;
    w.officers[42].is_available = 0;
    w.officers[42].is_captive = 1;
    w.officers[42].rank = 6;
    w.areas[0].officer_id = 0;
    ctx = make_ctx(w, SAN14_AREA_SYNC_PRE_TICK, 1, 2070201);
    assert(policy.on_sync(policy.plugin_context, &ctx, actions.data(), 16, &out_count) == SAN14_OK);
    assert(out_count == 0);

    std::vector<uint8_t> blob;
    assert(st.consume_dirty(blob) && !blob.empty());

    // 3. Reload into fresh state simulating save/load.
    policy_ns::State restored;
    restored.set_enabled(true);
    assert(restored.load_blob(blob.data(), blob.size()));
    auto restored_policy = policy_ns::make_policy(restored);

    // 4. Officer released back to player force 1 and area is vacant -> emits restore with dev=3, aux=2.
    w.officers[42].is_captive = 0;
    w.officers[42].is_serving = 1;
    w.officers[42].is_available = 1;
    w.officers[42].rank = 2;
    w.officers[42].force_id = 1;
    ctx = make_ctx(w, SAN14_AREA_SYNC_PRE_TICK, 1, 2070301);
    assert(restored_policy.on_sync(restored_policy.plugin_context, &ctx, actions.data(), 16, &out_count) == SAN14_OK);
    assert(out_count == 1);
    assert(actions[0].area_id == 12 && actions[0].officer_id == 42);
    assert(actions[0].restore_modes == 1 && actions[0].dev_type == 3 && actions[0].aux_mode == 2);

    // 5. Loading an earlier save date prunes future suspended entries.
    w.areas[0].officer_id = 0;
    w.areas[0].force_id = 2;  // lost to enemy at 2070501
    w.officers[42].is_captive = 1;
    w.officers[42].is_serving = 0;
    w.officers[42].is_available = 0;
    policy_ns::State st2;
    st2.set_enabled(true);
    {
        std::lock_guard<std::mutex> lock(st2.mutex);
        st2.suspended[12] = {12, 42, 3, 2, 2070501};
    }
    w.areas[0].force_id = 1;
    w.officers[42].is_captive = 0;
    w.officers[42].is_serving = 1;
    w.officers[42].is_available = 1;
    ctx = make_ctx(w, SAN14_AREA_SYNC_POST_SAVE_LOAD, 1, 2070110);  // earlier than 2070501
    assert(policy_ns::on_sync(&st2, &ctx, actions.data(), 16, &out_count) == SAN14_OK);
    assert(out_count == 0);

    std::cout << "persistent_area_policy_tests OK\n";
    return 0;
}
