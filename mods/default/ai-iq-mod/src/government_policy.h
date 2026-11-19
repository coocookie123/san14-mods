#pragma once
// Companion business policy, ported without priority/heuristic changes from
// SAN14-Companion/government_ranking_gate.js. No game addresses belong here.
#include "san14_government.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>
namespace companion_government_policy {
inline double clamp01(double n){return !std::isfinite(n)||n<=0?0:n>=1?1:n;}
inline int rounded(double n){return static_cast<int>(std::floor(n+0.5));}
inline bool water_terrain(int id){return id==12||id==13||id==14||id==18||id==21||id==24;}
inline int hex_distance(int a,int b){
    if(a<0||a>0xbd0f||b<0||b>0xbd0f)return 9999;
    const int y1=a/220,x1=a-y1*220,y2=b/220,x2=b-y2*220;
    const int dx=std::abs(x2-x1),dy2=std::abs(2*(y2-y1)-((x1&1)==0?1:0)+((x2&1)==0?1:0));
    return dy2>=dx?dx+((dy2-dx)>>1):dx;
}
inline int department(int id){
    if(id==46||(id>=1&&id<=9))return 0;
    if(id==47||(id>=10&&id<=18))return 1;
    if(id==48||(id>=19&&id<=27))return 2;
    if(id==49||(id>=28&&id<=36))return 3;
    if(id==50||(id>=37&&id<=45))return 4;
    return -1;
}
inline bool offensive(int id){return id==2||id==3||id==5||id==6||id==7||id==8;}
inline bool defensive(int id){return id==1||id==4;}
inline bool defensive_support(int id){return id==10||id==11||id==13||id==14||id==21;}
struct PolicyRule{int base,levelWeight;};
inline PolicyRule rule_for(int id){switch(id){
    case 46: return {32000,200};
    case 47: return {31400,200};
    case 49: return {31200,200};
    case 50: return {31000,200};
    case 48: return {30600,200};
    case 2: return {20000,500};
    case 3: return {19900,500};
    case 1: return {19800,500};
    case 8: return {19600,500};
    case 6: return {19500,500};
    case 5: return {19300,500};
    case 4: return {19200,500};
    case 7: return {19100,500};
    case 11: return {18500,500};
    case 10: return {18200,500};
    case 28: return {14500,450};
    case 29: return {14200,450};
    case 31: return {14000,450};
    case 38: return {12000,400};
    case 37: return {8000,300};
    case 40: return {8000,350};
    case 12: return {7800,350};
    case 13: return {7700,350};
    case 30: return {7600,350};
    case 41: return {7400,350};
    case 23: return {7300,350};
    case 24: return {7200,350};
    case 14: return {7150,350};
    case 21: return {7100,350};
    case 39: return {8500,350};
    case 19: return {3800,200};
    case 32: return {600,100};
    case 20: return {400,100};
    default:return {6500,300};
}}
struct Context {
    bool ready=false;
    int forceId=0,cityCount=1,orders=60,totalTroops=0,totalFood=0,totalGold=0;
    double waterRatio=0,waterFrontRatio=0,frontlineRatio=0,threat=0,war=0;
    double relativeStrength=0.5,forceDisadvantage=0,defensivePosture=0,waterThreat=0;
    double navalPriority=0,orderPressure=0,supplyPressure=0,empire=0;
    int advisorId=0,advisorPolicyId=0,advisorPolicyLevel=0,currentNavalLevel=0,switchPenalty=4500;
};
inline int score_policy(int id,int effLv,const Context& ctx,const San14GovernmentCandidate& info){
    const int lv=std::max(1,std::min(10,effLv));
    const auto rule=rule_for(id);int score=rule.base+lv*rule.levelWeight;
    const double war=clamp01(ctx.war),threat=clamp01(ctx.threat);
    const double water=clamp01(std::max(ctx.waterRatio,ctx.waterFrontRatio));
    const double orders=clamp01(ctx.orderPressure),supply=clamp01(ctx.supplyPressure);
    const double empire=clamp01(ctx.empire),frontline=clamp01(ctx.frontlineRatio);
    const double disadvantage=clamp01(ctx.forceDisadvantage);
    const double defensivePosture=clamp01(std::max(ctx.defensivePosture,threat*0.62+disadvantage*0.48+frontline*0.18));
    const double waterThreat=clamp01(ctx.waterThreat);
    const double navalPriority=clamp01(std::max({ctx.navalPriority,waterThreat*0.82+ctx.waterFrontRatio*0.42,water*war*(0.62+defensivePosture*0.45)}));
    const int dept=department(id),currentNavalLevel=std::max(0,std::min(10,ctx.currentNavalLevel));
    const int navalLevelGap=std::max(0,10-currentNavalLevel);
    if (dept == 0) score += 2200 + rounded(4200 * war + 2600 * threat);
    if (id == 46) score += rounded(3500 * war + 2000 * threat);
    if (id == 10) score += rounded(4000 * war + 3000 * threat);
    if (id == 11) score += rounded(5000 * war + 5500 * supply + 1500 * frontline);
    if (id == 12) score += rounded(3000 * war + 4500 * supply + 2000 * frontline);
    if (id == 13 || id == 14) score += rounded(2500 * threat + 1800 * frontline);
    if (id == 19) score += rounded(2500 * war);
    if (id == 20 || id == 21 || id == 32) score += rounded(5000 * threat + 1200 * frontline);
    if (id == 23 || id == 24) score += rounded(2200 * frontline + 1200 * empire);
    if (id == 28 || id == 31) score += rounded(2200 * war + 2500 * threat + 1200 * empire);
    if (id == 29 || id == 30) score += rounded(3500 * (1 - war) + 1800 * empire);
    if (id == 38) score += rounded(3500 * empire + 1200 * (1 - war));
    if (id == 40 || id == 41) score += rounded(2600 * (1 - threat) + 800 * empire);

    // A battle department is not synonymous with offense.  When a force is
    // strategically weaker and defending its cities, siege weapons and aggressive
    // formations lose value while compact defensive formations and sustainment rise.
    if (offensive(id)) {
      const double siegeBias = id == 8 ? 1.22 : 1.0;
      score -= rounded((8500 + 9500 * disadvantage) * defensivePosture * siegeBias);
    } else if (defensive(id)) {
      score += rounded(7000 * defensivePosture + 3500 * disadvantage);
    }
    if (defensive_support(id)) {
      score += rounded(5000 * defensivePosture + 2500 * disadvantage);
    }

    // Command books are valuable only when expected demand exceeds the current stock.
    if (id == 37) score += rounded(15000 * orders + 2500 * empire + 1200 * war);

    // Naval value is inferred from geography/fronts, never from a ruler-name whitelist.
    if (id == 39) {
      score += rounded(
        15000 * water +
        6500 * water * war +
        6500 * ctx.waterFrontRatio +
        31000 * navalPriority
      );
      if (navalPriority >= 0.38) {
        if (navalLevelGap > 0) {
          const int usefulLevel = std::min(lv, navalLevelGap);
          score += 11000 + navalLevelGap * 650 + usefulLevel * 900;
        } else if (info.current) {
          score += 7500; // keep the officers that already sustain the level-10 effect
        } else {
          score -= 26000; // level 10 reached: stop consuming extra personnel slots
        }
      } else if (water >= 0.28 && lv >= 3) {
        score += 4500; // discrete ship-unlock / threshold value
      }
    } else if (dept == 4 && navalPriority >= 0.38 && navalLevelGap > 0) {
      // Personnel slots are the only way to raise 培养水军. While a genuinely
      // water-dependent force is below level 10, incumbent hysteresis must not
      // let unrelated personnel policies occupy the last slots indefinitely.
      score -= rounded(20000 + 18000 * navalPriority + navalLevelGap * 1200);
    }

    // The advisor contributes their policy for free.  Exact duplicates therefore
    // receive only their remaining marginal level up to the native level-10 cap.
    if (ctx.advisorPolicyId == id && ctx.advisorPolicyLevel > 0) {
      const int remaining = std::max(0, 10 - ctx.advisorPolicyLevel);
      const int useful = std::min(lv, remaining);
      if (useful <= 0) score -= 24000;
      else score = rounded(score * (0.45 + 0.55 * (static_cast<double>(useful) / lv)));
    }

    // Reorganizing government disables policy effects for one turn.  Incumbents
    // get the estimated outage value as hysteresis; challengers must repay it.
    if (info.current) {
      double retention = 1;
      if (offensive(id)) {
        retention *= std::max(0.08, 1 - defensivePosture * (0.72 + 0.20 * disadvantage));
      } else if (id != 39 && navalPriority >= 0.38) {
        retention *= std::max(0.28, 1 - navalPriority * 0.58);
      } else if (!defensive(id) && !defensive_support(id)) {
        retention *= std::max(0.55, 1 - defensivePosture * 0.35);
      }
      score += rounded(std::max(0, ctx.switchPenalty) * retention);
    }
    const int cost = std::max(1, info.cost);
    score -= std::min(1800, (cost - 1) * 180);
    return score;
}
inline Context strategic_context(const San14GovernmentSnapshot& snapshot){
    Context context;context.forceId=snapshot.force_id;
    if(!snapshot.ready)return context;
    struct City {int tile,troops;double water=0;bool front=false,threatened=false;};
    std::vector<City> own;std::vector<int> enemy_cities;
    std::array<int,52> enemy_strength{},enemy_city_count{};
    int total_troops=0,total_food=0,total_gold=0,water=0,sampled=0,front_water=0,front_sampled=0;
    auto enemy=[&](int force){return force>0&&force<52&&snapshot.enemy_forces[force]!=0;};
    for(uint32_t i=0;i<std::min(snapshot.city_count,SAN14_GOVERNMENT_MAX_CITIES);++i){
        const auto& city=snapshot.cities[i];
        if(city.force_id==snapshot.force_id){
            int water_count=0;for(int terrain=0;terrain<256;++terrain)if(water_terrain(terrain))water_count+=city.terrain_counts[terrain];
            own.push_back({city.tile_id,city.troops,city.sampled_tiles?clamp01(double(water_count)/city.sampled_tiles):0});
            total_troops+=city.troops;total_food+=city.food;total_gold+=city.gold;
            water+=water_count;sampled+=city.sampled_tiles;
        }else if(enemy(city.force_id)){
            enemy_cities.push_back(city.tile_id);enemy_strength[city.force_id]+=city.troops;++enemy_city_count[city.force_id];
        }
    }
    int frontline_count=0;
    for(auto& city:own){
        int nearest=9999;for(int tile:enemy_cities)nearest=std::min(nearest,hex_distance(city.tile,tile));
        city.front=nearest<=34;if(city.front)++frontline_count;
    }
    for(uint32_t i=0;i<std::min(snapshot.city_count,SAN14_GOVERNMENT_MAX_CITIES);++i){
        const auto& city=snapshot.cities[i];if(city.force_id!=snapshot.force_id)continue;
        bool front=false;for(const auto& candidate:own)if(candidate.tile==city.tile_id&&candidate.front){front=true;break;}
        if(front){for(int terrain=0;terrain<256;++terrain)if(water_terrain(terrain))front_water+=city.terrain_counts[terrain];front_sampled+=city.sampled_tiles;}
    }
    int enemy_near=0,own_on_map=0,own_near=0,enemy_water_near=0;
    for(uint32_t i=0;i<std::min(snapshot.unit_count,SAN14_GOVERNMENT_MAX_UNITS);++i){
        const auto& unit=snapshot.units[i];
        if(unit.force_id==snapshot.force_id){
            own_on_map+=unit.troops;for(const auto& city:own)if(hex_distance(unit.tile_id,city.tile)<=12){own_near+=unit.troops;break;}
            continue;
        }
        if(!enemy(unit.force_id))continue;
        enemy_strength[unit.force_id]+=unit.troops;
        for(auto& city:own){
            const int distance=hex_distance(unit.tile_id,city.tile);
            if(distance<=24&&city.water>=0.08&&water_terrain(unit.terrain_id))
                enemy_water_near+=rounded(unit.troops*(distance<=8?1:distance<=16?0.65:0.35));
            if(distance<=12){enemy_near+=rounded(unit.troops*(distance<=4?1:distance<=8?0.65:0.35));city.threatened=true;break;}
        }
    }
    int threatened=0;for(const auto& city:own)if(city.threatened)++threatened;
    total_troops+=own_on_map;const int city_count=std::max(1,static_cast<int>(own.size()));
    int strongest_enemy=0;for(int force=1;force<52;++force)strongest_enemy=std::max(strongest_enemy,enemy_strength[force]+enemy_city_count[force]*15000);
    const int own_strength=total_troops+city_count*15000;
    const double relative=strongest_enemy>0?clamp01(double(own_strength)/std::max(1,own_strength+strongest_enemy)):0.5;
    const double disadvantage=clamp01((0.50-relative)*2.5),frontline=clamp01(double(frontline_count)/city_count);
    const int garrison=std::max(0,total_troops-own_on_map);
    const double local_defense=garrison*0.22+own_near*0.35;
    const double threat_troops=clamp01(enemy_near/std::max(5000.0,local_defense));
    const double threat_cities=clamp01(double(threatened)/city_count);
    const double threat=clamp01(threat_troops*0.65+threat_cities*0.55),war=clamp01(std::max(threat,frontline*0.72));
    const int expected_orders=std::max(20,12+city_count*6+rounded(14*war));
    const double needed_food=std::max(1.0,total_troops*(2.2+war*1.4));
    const double water_ratio=sampled?clamp01(double(water)/sampled):0;
    const double front_water_ratio=front_sampled?clamp01(double(front_water)/front_sampled):water_ratio*frontline;
    const double water_threat=clamp01(enemy_water_near/std::max(5000.0,total_troops*0.22));
    const double defensive_posture=clamp01(threat*0.62+disadvantage*0.48+frontline*0.18);
    context.ready=true;context.cityCount=city_count;context.orders=snapshot.orders;
    context.totalTroops=total_troops;context.totalFood=total_food;context.totalGold=total_gold;
    context.waterRatio=water_ratio;context.waterFrontRatio=front_water_ratio;context.frontlineRatio=frontline;
    context.threat=threat;context.war=war;context.relativeStrength=relative;context.forceDisadvantage=disadvantage;
    context.defensivePosture=defensive_posture;context.waterThreat=water_threat;
    context.navalPriority=clamp01(std::max(water_threat*0.82+front_water_ratio*0.42,water_ratio*war*(0.62+defensive_posture*0.45)));
    context.orderPressure=clamp01(double(expected_orders-snapshot.orders)/expected_orders);
    context.supplyPressure=clamp01((needed_food-total_food)/needed_food);context.empire=clamp01(double(city_count-2)/12);
    context.advisorId=snapshot.advisor_id;context.advisorPolicyId=snapshot.advisor_policy_id;context.advisorPolicyLevel=snapshot.advisor_policy_level;
    context.currentNavalLevel=snapshot.current_naval_level;
    context.switchPenalty=rounded(4000+3000*war+9000*threat+snapshot.current_officer_count*120);
    return context;
}
inline San14Result SAN14_MOD_CALL score(void*,const San14GovernmentSnapshot* snapshot,const San14GovernmentCandidate* candidate,int32_t* output){
    if(!snapshot||!candidate||!output||snapshot->struct_size<sizeof(*snapshot)||candidate->struct_size<sizeof(*candidate))return SAN14_E_INVALID_ARGUMENT;
    static thread_local uint64_t cached_id=UINT64_MAX;
    static thread_local Context cached;
    if(cached_id!=snapshot->snapshot_id){cached=strategic_context(*snapshot);cached_id=snapshot->snapshot_id;}
    *output=score_policy(candidate->policy_id,candidate->effective_level,cached,*candidate);
    return SAN14_OK;
}
} // namespace companion_government_policy
