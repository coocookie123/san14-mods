#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <array>
#include <cassert>
#include <iostream>
#include <unordered_map>
#include <vector>

#include "../src/tactical_ai_policy.h"

namespace tac_ns = companion_mod::tactical_ai_policy;

namespace {
struct MockTacEnv {
    std::unordered_map<uint32_t, San14TacticalTile> tiles;
    std::unordered_map<uint32_t, San14TacticalOfficer> officers;
    std::unordered_map<uint32_t, San14OfficerTraits> traits;
    std::vector<uint32_t> returned_slots;
    std::vector<San14TacticalDeployRequest> deployed;
};

uint32_t tile_at(uint32_t x, uint32_t y) { return y * 220u + x; }

San14Result SAN14_MOD_CALL mock_get_tile(void* ctx, uint32_t id, San14TacticalTile* out) {
    auto* env = static_cast<MockTacEnv*>(ctx);
    auto it = env->tiles.find(id);
    if (it != env->tiles.end()) {
        *out = it->second;
    } else {
        *out = {};
        out->struct_size = sizeof(*out);
        out->tile_id = id;
        out->terrain_kind = 1;  // passable plain
    }
    return SAN14_OK;
}

int32_t SAN14_MOD_CALL mock_hex_dist(void*, uint32_t a, uint32_t b) { return tac_ns::hex_dist(a, b); }

San14Result SAN14_MOD_CALL mock_get_officer(void* ctx, uint32_t id, San14TacticalOfficer* out) {
    auto* env = static_cast<MockTacEnv*>(ctx);
    auto it = env->officers.find(id);
    if (it == env->officers.end()) return SAN14_E_NOT_FOUND;
    *out = it->second;
    return SAN14_OK;
}
San14Result SAN14_MOD_CALL mock_get_officer_traits(void* ctx,uint32_t id,San14OfficerTraits* out){
    const auto& entries=static_cast<MockTacEnv*>(ctx)->traits;
    const auto found=entries.find(id);if(found==entries.end())return SAN14_E_UNSUPPORTED;
    *out=found->second;return SAN14_OK;
}

San14Result SAN14_MOD_CALL mock_list_city_officers(
    void* ctx, uint32_t city_id, San14TacticalOfficer* out, uint32_t cap, uint32_t* out_count
) {
    auto* env = static_cast<MockTacEnv*>(ctx);
    uint32_t n = 0;
    for (const auto& kv : env->officers) {
        if (kv.second.home_global_id != city_id * 100u || kv.second.has_active_unit || kv.second.has_active_command) {
            continue;
        }
        if (out && n < cap) out[n] = kv.second;
        ++n;
    }
    *out_count = n;
    return (out && n > cap) ? SAN14_E_BUFFER_TOO_SMALL : SAN14_OK;
}

int32_t SAN14_MOD_CALL mock_enemy(void*, uint32_t a, uint32_t b) { return (a && b && a != b) ? 1 : 0; }

San14Result SAN14_MOD_CALL mock_return_unit(void* ctx, uint32_t slot, int32_t, int32_t, int32_t) {
    static_cast<MockTacEnv*>(ctx)->returned_slots.push_back(slot);
    return SAN14_OK;
}

San14Result SAN14_MOD_CALL mock_deploy_unit(
    void* ctx, const San14TacticalDeployRequest* req, San14TacticalDeployResult* res
) {
    auto* env = static_cast<MockTacEnv*>(ctx);
    // Every sortie here has a direct target and no intermediate stops.
    for (uint32_t tile : req->waypoint_tiles) assert(tile == SAN14_TACTICAL_INVALID_TILE_ID);
    env->deployed.push_back(*req);
    res->struct_size = sizeof(*res);
    res->unit_slot = static_cast<uint32_t>(400 + env->deployed.size());
    res->deployed_troops = req->troops;
    return SAN14_OK;
}
}  // namespace

int main() {
    tac_ns::State st;
    auto policy = tac_ns::make_policy(st);

    MockTacEnv env;
    // Place water tile for enemy fleet approaching waterfront city at (20,20).
    const uint32_t enemy_water_tile = tile_at(24, 20);
    env.tiles[enemy_water_tile] = {sizeof(San14TacticalTile), enemy_water_tile, 12, 1, 1, 1, 0, 10, 1, {}};
    const uint32_t station_water_tile = tile_at(23, 20);
    env.tiles[station_water_tile] = {sizeof(San14TacticalTile), station_water_tile, 12, 1, 1, 1, 0, 0, 1, {}};

    // Officers for city 1 (hgid=100) and reserve city 2 (hgid=200)
    San14TacticalOfficer land_off{sizeof(San14TacticalOfficer), 10, 2, 1, 100, 100, 65, 65, 50, 0x7, 11, 7000, {}, 0, 0, 0, 0};
    San14TacticalOfficer naval_off{sizeof(San14TacticalOfficer), 20, 2, 1, 100, 100, 82, 78, 60, 0x7, 12, 10000, {27}, 0, 0, 0, 0};
    San14TacticalOfficer relief_off{sizeof(San14TacticalOfficer), 30, 2, 1, 200, 200, 80, 75, 60, 0x7, 11, 9000, {}, 0, 0, 0, 0};
    env.officers[10] = land_off;
    env.officers[20] = naval_off;
    env.officers[30] = relief_off;

    std::vector<San14TacticalDistrict> districts = {
        {sizeof(San14TacticalDistrict), 1, 1, 3, 0, {}},
    };
    std::vector<San14TacticalCity> cities = {
        {sizeof(San14TacticalCity), 1, 1, 1, 6000, 26000, 200000, 100, tile_at(20, 20), 100, 0, {}},
        {sizeof(San14TacticalCity), 2, 1, 1, 15000, 45000, 250000, 100, tile_at(38, 20), 200, 0, {}},
    };
    std::vector<San14TacticalUnit> units = {
        // Enemy water unit near city 1
        {sizeof(San14TacticalUnit), 10, 2, 900, 1, 901, 5000, 100, 1, 12, 0, enemy_water_tile, 0, {}, 1, 0, 0, tile_at(20, 20), 0, 0, 0, 0, 0, 0, 0, {}, 5000.f},
        // Enemy siege engine near city 1
        {sizeof(San14TacticalUnit), 11, 2, 900, 1, 902, 4000, 100, 10, 11, 0, tile_at(16, 20), 0, {}, 1, 0, 0, tile_at(20, 20), 0, 0, 0, 0, 0, 0, 0, {}, 4000.f},
        // Distant transport belonging to city 1 (> 6 hexes away)
        {sizeof(San14TacticalUnit), 12, 1, 100, 2, 41, 3000, 100, 1, 11, 0, tile_at(35, 20), 0, {}, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}, 1000.f},
        // Distant AI combat unit to recall
        {sizeof(San14TacticalUnit), 13, 1, 100, 1, 42, 3000, 100, 1, 11, 0, tile_at(50, 20), 0, {}, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}, 3000.f},
        // Friendly local AI combat unit near enemy siege
        {sizeof(San14TacticalUnit), 14, 1, 100, 1, 43, 6000, 100, 1, 11, 0, tile_at(18, 20), 0, {}, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, {}, 6000.f},
    };

    San14TacticalWorld world{};
    world.struct_size = sizeof(world);
    world.year = 207;
    world.month = 5;
    world.day = 1;
    world.now_ms = 50000;
    world.player_force_id = 3;
    world.districts = districts.data();
    world.district_count = static_cast<uint32_t>(districts.size());
    world.cities = cities.data();
    world.city_count = static_cast<uint32_t>(cities.size());
    world.units = units.data();
    world.unit_count = static_cast<uint32_t>(units.size());

    San14TacticalHostOps ops{};
    ops.struct_size = sizeof(ops);
    ops.ops_context = &env;
    ops.get_tile = &mock_get_tile;
    ops.hex_distance = &mock_hex_dist;
    ops.get_officer = &mock_get_officer;
    ops.list_city_officers = &mock_list_city_officers;
    ops.is_enemy_force = &mock_enemy;
    ops.return_unit = &mock_return_unit;
    ops.deploy_unit = &mock_deploy_unit;

    // New Hosts expose the extra physical slots without resizing old officer arrays.
    const auto base_score=tac_ns::naval_officer_score(naval_off,&ops);
    ops.get_officer_traits=&mock_get_officer_traits;
    San14OfficerTraits extra{sizeof(extra)};extra.officer_id=20;extra.active_slots=9;
    extra.ids[0]=27;extra.ids[5]=27;extra.ids[8]=11;env.traits[20]=extra;
    assert(tac_ns::naval_officer_score(naval_off,&ops)==base_score+340);
    env.traits[20].active_slots=5;
    assert(tac_ns::naval_officer_score(naval_off,&ops)==base_score);
    env.traits[20].active_slots=9;ops.struct_size=offsetof(San14TacticalHostOps,get_officer_traits);
    assert(tac_ns::naval_officer_score(naval_off,&ops)==base_score);
    ops.struct_size=sizeof(ops);env.traits[20].officer_id=21;
    assert(tac_ns::naval_officer_score(naval_off,&ops)==base_score);
    env.traits.clear();assert(tac_ns::naval_officer_score(naval_off,&ops)==base_score);
    ops.get_officer_traits=nullptr;

    // 1. Sub-feature isolation: only naval enabled -> emergency reserve hook stays UNSUPPORTED.
    st.set_config({true, false, false});
    int32_t new_res = 0, new_quota = 0;
    assert(policy.on_eval_candidate_reserve(policy.plugin_context, &world, &ops, 1, 5000, 10000, &new_res, &new_quota) == SAN14_E_UNSUPPORTED);

    // Naval officer promotion chooses 20 (helmsman + naval_formation 12) and station water tile.
    uint32_t cands[2] = {10, 20};
    uint32_t promoted = 0, override_tile = 0;
    assert(policy.on_select_defense_officer(policy.plugin_context, &world, &ops, 1, 0, enemy_water_tile, cands, 2, &promoted, &override_tile) == SAN14_OK);
    assert(promoted == 20 && override_tile == station_water_tile);

    // 2. Enable emergency reinforcement -> target supplement, reserve floor 15k, wealth boost, transport filter, rollback.
    st.set_config({true, true, true});
    int32_t corrected = 0;
    assert(policy.on_eval_target_strength(policy.plugin_context, &world, &ops, 1, 1, 10000, 5000, &corrected) == SAN14_OK);
    assert(corrected > 10000);
    assert(policy.on_eval_candidate_reserve(policy.plugin_context, &world, &ops, 2, 5000, 10000, &new_res, &new_quota) == SAN14_OK);
    assert(new_res == 15000 && new_quota == 30000);  // city 2 has gold>=10k, food>=40k, surplus 45k-15k=30k
    int32_t force_skip = 0;
    assert(policy.on_eval_command_rollback(policy.plugin_context, 1, 0, &force_skip) == SAN14_OK && force_skip == 1);
    int32_t inc = 1;
    assert(policy.on_eval_transport_include(policy.plugin_context, &world, &ops, 12, 1, &inc) == SAN14_OK && inc == 0);

    // 3. Siege interception redirects unit 14 onto enemy siege unit 11.
    San14TacticalOrderProposal prop{sizeof(San14TacticalOrderProposal)};
    assert(policy.on_filter_order_target(policy.plugin_context, &world, &ops, 14, 0, 0, &prop) == SAN14_OK);
    assert(prop.decision == SAN14_TACTICAL_ORDER_REPLACE_ROUTE && prop.target_id == 11);
    assert(prop.target_type == 0x1b);
    for (uint32_t tile : prop.waypoints) assert(tile == SAN14_TACTICAL_INVALID_TILE_ID);

    // Each setting combination starts with fresh assignment/cooldown state.
    // Both single-feature paths must omit waypoints, as must combined sorties.
    for (unsigned mask = 0; mask < 8; ++mask) {
        tac_ns::State isolated;
        isolated.set_config({(mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0});
        auto isolated_policy = tac_ns::make_policy(isolated);
        MockTacEnv isolated_env = env;
        isolated_env.deployed.clear();
        auto isolated_ops = ops;
        isolated_ops.ops_context = &isolated_env;
        isolated_policy.on_strategy_update(isolated_policy.plugin_context, &world, &isolated_ops, SAN14_TACTICAL_PHASE_PLANNING);
        assert(isolated_env.deployed.empty() == ((mask & 5) == 0));
        for (const auto& request : isolated_env.deployed) {
            assert(request.target_type == 9 || request.target_type == 0x1b);
            assert(request.target_id != 0);
        }
        San14TacticalOrderProposal isolated_prop{sizeof(isolated_prop)};
        isolated_policy.on_filter_order_target(isolated_policy.plugin_context, &world, &isolated_ops, 14, 9, 1234, &isolated_prop);
        assert((isolated_prop.decision == SAN14_TACTICAL_ORDER_REPLACE_ROUTE) == ((mask & 4) != 0));
        if (mask & 4) {
            assert(isolated_prop.target_type == 0x1b && isolated_prop.target_id == 11);
            for (uint32_t tile : isolated_prop.waypoints) assert(tile == SAN14_TACTICAL_INVALID_TILE_ID);
        }
    }

    // Moving the enemy changes its tile, not the identity we attack.
    const auto previous_tile = units[1].tile_id;
    ++units[1].tile_id;
    assert(policy.on_filter_order_target(policy.plugin_context, &world, &ops, 14, 9, previous_tile, &prop) == SAN14_OK);
    assert(prop.target_type == 0x1b && prop.target_id == 11);
    units[1].tile_id = previous_tile;

    // 4. Strategy pass deploys naval/siege defense waves while city 1 has surplus.
    policy.on_strategy_update(policy.plugin_context, &world, &ops, SAN14_TACTICAL_PHASE_PLANNING);
    assert(!env.deployed.empty());

    // Simulate the deployed unit appearing in the world snapshot before retreat eval.
    San14TacticalUnit dep_unit = units[4];
    dep_unit.slot = 401;
    dep_unit.troops = env.deployed[0].troops;
    units.push_back(dep_unit);
    world.units = units.data();
    world.unit_count = static_cast<uint32_t>(units.size());

    // Protect deployed assignment against retreat when city threat remains active.
    int32_t retreat_ov = -1;
    assert(policy.on_eval_retreat(policy.plugin_context, &world, &ops, 401, 1, &retreat_ov) == SAN14_OK);
    assert(retreat_ov == 0);

    // 5. When city 1 becomes outnumbered (< threat 9000), distant field unit 13 is recalled
    //    and adjacent reserve city 2 dispatches a relief sortie.
    const size_t before_relief = env.deployed.size();
    cities[0].troops = 6000;
    policy.on_strategy_update(policy.plugin_context, &world, &ops, SAN14_TACTICAL_PHASE_PLANNING);
    assert(env.returned_slots.size() == 1 && env.returned_slots[0] == 13);
    assert(env.deployed.size() > before_relief);

    std::cout << "tactical_ai_policy_tests OK\n";
    return 0;
}
