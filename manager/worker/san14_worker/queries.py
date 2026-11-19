"""MCP query filtering and presentation over typed GameAdapter records."""
from __future__ import annotations
import asyncio
from mcp.server.mcpserver import MCPServer
from mcp.types import ToolAnnotations
from mcp.server.mcpserver.exceptions import ToolError
from .snapshot import Snapshot, QueryContext, current_query, snap, neighbors, hex_distance, tile_of

def force_names(snapshot):
    return snapshot.force_names

def _find_city(s: Snapshot, key: str | int) -> dict:
    for c in s.cities.values():
        if str(c['id']) == str(key) or c['name'] == str(key):
            return c
    raise ValueError(f'没有叫 {key} 的城')

def _officer_row(s: Snapshot, o: dict, in_unit: set[int]) -> dict:
    loc = s.cities.get(o['location'])
    return {'id': o['id'], 'name': o['name'], 'rank': o['rank'], 'loyalty': o['loyalty'], 'loyalty_raw': o['loyalty_raw'], 'stats_kind': 'base', 'effective_stats': None, 'location': loc['name'] if loc else o['location'], 'location_id': o['location'], 'in_unit': o['id'] in in_unit, 'task': None, 'can_act': None, 'can_dispatch': None, 'command_actions': o.get('command_actions'), 'assignments': o.get('assignments', []), 'command_actions_status': o.get('command_actions_status'), 'liked_officers': o.get('liked_officers', []), 'disliked_officers': o.get('disliked_officers', []), 'force_id': o['force'], 'district_id': o['district'], **o['stats']}

def _unit_row(s: Snapshot, u: dict, names: dict[int, str]) -> dict:
    return {'slot': u['slot'], 'commander': u['commander_name'], 'commander_id': u['commander'], 'force': names.get(u['force'], u['force']), 'force_id': u['force'], 'troops': u['troops'], 'pos': u['pos'], 'target': u['target'], 'target_detail': u['target_detail'], 'formation': u['formation'], 'morale': u['morale'], 'order_code': u['order_code'], 'unit_type_code': u.get('unit_type_code'), 'unit_kind': u.get('unit_kind'), 'cargo': u.get('cargo'), 'order_name': u.get('order_name', s.order_names.get(u['order_code'])), 'waypoints': u['waypoints'], 'next_steps': None, 'supply_status': None, 'unknown_fields': (['order_name'] if u['order_code'] not in s.order_names else []) + ['supply_status', 'waypoint_execution_order']}

def get_overview() -> dict:
    """全局局势：日期、玩家势力资源、所有势力城数和兵力，以及非本势力的接壤城市。接壤不代表敌对。"""
    s = snap()
    names = force_names(s)
    by_force: dict[int, dict] = {fid: {'force_id': fid, 'force': f['name'], 'cities': [], 'troops': 0, 'gold': 0, 'food': 0} for fid, f in s.forces.items()}
    for c in s.cities.values():
        if c['force'] == 0:
            continue
        f = by_force.setdefault(c['force'], {'force_id': c['force'], 'force': names[c['force']], 'cities': [], 'troops': 0, 'gold': 0, 'food': 0})
        f['cities'].append(c['name'])
        f['troops'] += c['troops']
        f['gold'] += c['gold']
        f['food'] += c['food']
    for u in s.units:
        if u['force'] != 0:
            f = by_force.setdefault(u['force'], {'force_id': u['force'], 'force': names.get(u['force'], '归属未知'), 'cities': [], 'troops': 0, 'gold': 0, 'food': 0})
            f['troops'] += u['troops']
    mine = by_force.get(s.player, {})
    officers = [o for o in s.officers.values() if o['force'] == s.player and o['rank_code'] in (1, 2, 3, 4)]
    my_tiles = {int(i) for i in (s.tile_forces == s.player).nonzero()[0]}
    touching = set()
    border_regions = {}
    for t in my_tiles:
        x, y = (t % 220, t // 220)
        for nx, ny in neighbors(x, y):
            terr = int(s.tiles['territory'][tile_of(nx, ny)])
            owner = int(s.tile_forces[tile_of(nx, ny)])
            if owner != s.player and terr:
                b = s.bases.get(terr)
                if b:
                    touching.add(b['city'])
                    border_regions.setdefault(b['city'], {})[terr] = {'region_id': terr, 'region': b['name'], 'force_id': owner if owner >= 0 else None, 'force': names.get(owner if owner >= 0 else None)}
    frontier = [{'city': s.cities[c]['name'], 'force': names[s.cities[c]['force']], 'force_id': s.cities[c]['force'], 'troops': s.cities[c]['troops'], 'border_force_ids': sorted({r['force_id'] for r in border_regions[c].values() if r['force_id'] is not None}), 'border_regions': list(border_regions[c].values())} for c in sorted(touching) if c in s.cities]
    my_centers = [c['center'] for c in s.cities.values() if c['force'] == s.player and c['center']]
    nearby = []
    for c in s.cities.values():
        if c['force'] == s.player or not c['center'] or (not my_centers) or (c['id'] > 46):
            continue
        d = min((hex_distance(c['center'], m) for m in my_centers))
        nearby.append({'city': c['name'], 'force': names.get(c['force']), 'troops': c['troops'], 'distance': d, 'center': c['center']})
    nearby.sort(key=lambda r: r['distance'])
    return {'snapshot': s.metadata, 'date': s.date['text'], 'player': {'force': names.get(s.player), **{k: v for k, v in mine.items() if k != 'force'}, 'officers': len(officers), 'units_on_map': sum((1 for u in s.units if u['force'] == s.player))}, 'frontier': frontier, 'nearest_other_cities': nearby[:8], 'forces': sorted(by_force.values(), key=lambda f: -f['troops']), 'limitations': ['接壤表示领土相接，不代表敌对；distance 是几何格距，不是行军时间']}

def list_cities(force: str='') -> list[dict]:
    """城市列表（兵力、金、粮、太守、中心坐标）。force 可填势力名或 "player"，留空列出全部。"""
    s = snap()
    names = force_names(s)
    rows = []
    for c in s.cities.values():
        fname = names.get(c['force'], str(c['force']))
        if force == 'player' and c['force'] != s.player:
            continue
        if force and force != 'player' and (force not in fname):
            continue
        rows.append({'id': c['id'], 'name': c['name'], 'force': fname, 'force_id': c['force'], 'district_id': c['district'], 'governor': s.officer_name(c['governor']) if c['governor'] else None, 'governor_id': c['governor'], 'troops': c['troops'], 'gold': c['gold'], 'food': c['food'], 'center': c['center']})
    return rows

def get_city(city: str) -> dict:
    """单城详情：数值、辖下县/港/关、城里的武将、附近 15 格内的部队。city 填城名或编号。"""
    s = snap()
    names = force_names(s)
    c = _find_city(s, city)
    in_unit = {u['commander'] for u in s.units}
    officers = [_officer_row(s, o, in_unit) for o in s.officers.values() if o['location'] == c['id'] and o['rank_code'] in (1, 2, 3, 4)]
    bases = [{'name': b['name'], 'kind': b['kind'], 'force': names.get(b['force'], b['force']), 'pos': b['pos']} for b in s.bases.values() if b['city'] == c['id'] and b['kind'] != '城市']
    center = c['center']
    near = [_unit_row(s, u, names) for u in s.units if center and u['pos'] and (hex_distance(u['pos'], center) <= 15)]
    return {'id': c['id'], 'name': c['name'], 'force': names.get(c['force']), 'governor': s.officer_name(c['governor']) if c['governor'] else None, 'governor_id': c['governor'], 'troops': c['troops'], 'gold': c['gold'], 'food': c['food'], 'center': c['center'], 'morale': c['morale'], 'durability': c['durability'], 'recorded_expenses': c.get('recorded_expenses'), 'tiles': c['tiles'], 'bases': bases, 'officers': officers, 'units_nearby': near, 'force_id': c['force'], 'district_id': c['district'], 'snapshot': s.metadata}

def list_officers(force: str='player', include_unavailable: bool=False) -> list[dict]:
    """武将列表（身份、忠诚、所在城、基础五维、亲爱/厌恶武将、是否任部队主将）。force 填 "player"、势力名，或 "all"。"""
    s = snap()
    names = force_names(s)
    in_unit = {u['commander'] for u in s.units}
    rows = []
    for o in s.officers.values():
        if not include_unavailable and o['rank_code'] not in (1, 2, 3, 4):
            continue
        if force == 'player' and o['force'] != s.player:
            continue
        if force not in ('player', 'all') and force not in names.get(o['force'], ''):
            continue
        row = _officer_row(s, o, in_unit)
        row['force'] = names.get(o['force'], o['force'])
        rows.append(row)
    return rows

def list_units(force: str='') -> list[dict]:
    """地图部队：主将、兵力、位置、分类目标和原始路径点（执行顺序未知）。force 填 player、势力名，留空列全部。"""
    s = snap()
    names = force_names(s)
    rows = []
    for u in s.units:
        fname = names.get(u['force'], str(u['force']))
        if force == 'player' and u['force'] != s.player:
            continue
        if force and force != 'player' and (force not in fname):
            continue
        rows.append(_unit_row(s, u, names))
    return rows

TERRAIN_CHARS = {0: '?', 1: '.', 2: '=', 3: ':', 4: 'w', 5: 'f', 6: 'F', 7: 'p', 8: 'h', 9: 'm', 10: 'M', 11: '/', 12: '~', 13: 'r', 14: 'R', 15: 'B', 16: 'C', 17: 'G', 18: 'P', 19: '^', 20: '^', 21: 'R', 22: '#', 23: ' ', 24: 'R'}

def get_terrain(x0: int, y0: int, x1: int, y1: int) -> dict:
    """矩形范围的地形图（最多 80x80 格）。每行一个字符串，x 从 x0 到 x1；奇数列比偶数列高半格。
    图例：. 平地  = 街道  : 沙地  w 湿地  f 森林  F 密林  p 毒沼  h 矮山  m 中山  M 高山  / 山路
    ~ 浅滩  r 河流  R 大河/急流/奔流  B 府(县)  C 城市  G 关隘  P 港口  ^ 险山/山崖  # 不可通行  空格 海/界外"""
    x0, x1, y0, y1 = terrain_bounds(x0, y0, x1, y1)
    if x1 - x0 + 1 > 80 or y1 - y0 + 1 > 80:
        raise ValueError('范围太大，最多 80x80')
    s = snap()
    terr = s.tiles['terrain']
    rows = [''.join((TERRAIN_CHARS.get(int(terr[tile_of(x, y)]), '?') for x in range(x0, x1 + 1))) for y in range(y0, y1 + 1)]
    names = force_names(s)
    marks = [{'name': b['name'], 'kind': b['kind'], 'force': names.get(b['force'], b['force']), 'pos': b['pos']} for b in s.bases.values() if b['pos'] and x0 <= b['pos'][0] <= x1 and (y0 <= b['pos'][1] <= y1)]
    units = [_unit_row(s, u, names) for u in s.units if u['pos'] and x0 <= u['pos'][0] <= x1 and (y0 <= u['pos'][1] <= y1)]
    return {'origin': [x0, y0], 'rows': rows, 'places': marks, 'units': units, 'snapshot': s.metadata}

def terrain_bounds(x0, y0, x1, y1):
    if not all((0 <= v <= 219 for v in (x0, y0, x1, y1))):
        raise ValueError('地图坐标必须在 0..219 内')
    x0, x1 = sorted((x0, x1))
    y0, y1 = sorted((y0, y1))
    return (x0, x1, y0, y1)

def find_force(s: Snapshot, key: str) -> int:
    if key == 'player':
        return s.player
    matches = [fid for fid, f in s.forces.items() if str(fid) == key or f['name'] == key or f['name'].removesuffix('军') == key]
    if len(matches) != 1:
        raise ValueError(f'势力不存在或不唯一：{key}；请使用势力编号或完整名称')
    return matches[0]

def get_officer(officer: str) -> dict:
    """武将详情：五维、个性、可用阵形、亲爱/厌恶武将、身份和出征状态；任务与行动资格尚未完整验证。"""
    s = snap()
    matches = [o for o in s.officers.values() if o['name'] == officer or str(o['id']) == officer]
    if len(matches) != 1:
        raise ValueError('武将不存在或同名，请提供武将编号')
    o = matches[0]
    return {'snapshot': s.metadata, **o, 'force_name': force_names(s).get(o['force']), 'location_name': s.cities.get(o['location'], {}).get('name'), 'units': [_unit_row(s, u, force_names(s)) for u in s.units if u['commander'] == o['id']], 'task': None, 'can_act': None, 'can_dispatch': None, 'task_target': None, 'task_deadline': None, 'unknown_fields': ['task', 'can_act', 'can_dispatch', 'effective_stats', 'task_target', 'task_deadline'], 'limitations': ['command_actions 仅为外部命令记录；无记录不证明空闲，类名不等于已验证界面任务名称'], 'field_coverage': {k: v for k, v in s.field_coverage.items() if k.startswith('officer.')}, 'evidence': 'docs/FIELD_EVIDENCE.md#officers'}

def get_map_area(x0: int, y0: int, x1: int, y1: int) -> dict:
    """最多30x30逐格地图：地形、势力、所属区域和城市。地格军团及通行耗时未知。"""
    x0, x1, y0, y1 = terrain_bounds(x0, y0, x1, y1)
    if x1 - x0 + 1 > 30 or y1 - y0 + 1 > 30:
        raise ValueError('逐格查询最多 30x30')
    s = snap()
    names = force_names(s)
    rows = []
    for y in range(y0, y1 + 1):
        for x in range(x0, x1 + 1):
            tile = s.tiles[tile_of(x, y)]
            base = s.bases.get(int(tile['territory']), {})
            owner = int(tile['owner'])
            force = int(s.tile_forces[tile_of(x, y)])
            force = force if force >= 0 else None
            rows.append({'pos': [x, y], 'terrain_id': int(tile['terrain']), 'terrain': s.terrain_at(x, y), 'district_id': None, 'owner_raw': owner, 'ownership_source': 'tile_owner', 'force_id': force, 'force': names.get(force, f'未知势力({force})'), 'region_id': int(tile['territory']), 'region': base.get('name'), 'city': s.cities.get(base.get('city'), {}).get('name'), 'passable': None, 'movement_cost': None})
    return {'snapshot': s.metadata, 'tiles': rows, 'unknown_fields': ['district_id', 'passable', 'movement_cost'], 'limitations': ['地形与道路可查看；尚不能计算特定阵形的实际可达性或行军天数']}

def economy_row(s: Snapshot, c: dict) -> dict:
    regions = [b for b in s.bases.values() if b['city'] == c['id']]
    owned = [b for b in regions if b['force'] == c['force'] and c['force'] not in (None, 0)]
    return {'id': c['id'], 'city': c['name'], 'force_id': c['force'], 'gold': c['gold'], 'food': c['food'], 'troops': c['troops'], 'morale': c['morale'], 'durability': c['durability'], 'assignments': c.get('assignments'), 'owned_region_population': sum((b['population'] for b in owned)), 'gross_region_income': {k: sum((b['income'][k] for b in owned)) for k in ['gold', 'food', 'recruits']}, 'income_period': None, 'expenses': {'gold': None, 'food': c['food_expense']}, 'recorded_expenses': c.get('recorded_expenses'), 'income_source': 'game_projection_cache', 'income_cache_freshness': 'unknown', 'zero_income_confirmed': False, 'aggregation_scope': 'regions geographically associated with this city and controlled by its owner', 'included_region_ids': [b['id'] for b in owned], 'expense_semantics': 'recorded_counter; future settlement amount and period unverified', 'net_income': None, 'regions': [{k: b[k] for k in ['id', 'name', 'kind', 'force', 'district', 'population', 'public_order', 'income', 'development_points', 'development_assignment'] if k in b} for b in regions], 'unknown_fields': ['income_period', 'gold_expense', 'net_income', 'development_levels'], 'field_coverage': {k: v for k, v in s.field_coverage.items() if k.startswith(('economy.', 'city.'))}}

def get_city_economy(city: str) -> dict:
    """城市经济：地区收入缓存、人口、治安、士气、耐久及已记录支出。刷新状态/周期未知，不是净收益预测。"""
    s = snap()
    return {'snapshot': s.metadata, **economy_row(s, _find_city(s, city)), 'evidence': 'docs/FIELD_EVIDENCE.md#economy', 'limitations': ['地区收入是游戏计算缓存，零值不证明真实收入为零；刷新时间未知', '地区征兵收入之和不是完整城市征兵总量', '记录支出计数与未来预算不同，不与缓存收入直接相减推算净收益', '开发点不等于开发等级']}

def get_economy(force: str='player') -> dict:
    """势力各城地区经济缓存和已记录支出；缓存刷新、完整收入周期与未来净收益未知。"""
    s = snap()
    fid = find_force(s, force)
    return {'snapshot': s.metadata, 'force': s.forces[fid]['name'], 'cities': [economy_row(s, c) for c in s.cities.values() if c['force'] == fid], 'limitations': ['收入来自地区预测缓存，刷新状态未知；记录支出不等于下一旬预测支出']}

TOOL_NAMES = frozenset({'list_units', 'get_map_area', 'list_officers', 'list_cities', 'get_officer', 'get_city', 'get_economy', 'get_city_economy', 'get_terrain', 'get_overview'})

def create_server():
    server=MCPServer('san14-semantic-queries')
    for name in sorted(TOOL_NAMES):
        server.tool(annotations=ToolAnnotations(read_only_hint=True,destructive_hint=False,
                    idempotent_hint=True,open_world_hint=False))(globals()[name])
    return server

def invoke(server,rpc,name,arguments,authority):
    token=current_query.set(QueryContext(rpc,name,authority))
    try:
        result=asyncio.run(server.call_tool(name,arguments))
        return result.model_dump(mode='json',by_alias=True,exclude_none=True)
    except ToolError as error:
        return {'content':[{'type':'text','text':str(error)}],'isError':True}
    finally:
        current_query.reset(token)
