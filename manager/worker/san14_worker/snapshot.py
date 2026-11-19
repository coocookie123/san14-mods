"""Semantic records supplied by GameAdapter; no process or memory view."""
from __future__ import annotations

from contextvars import ContextVar
from types import SimpleNamespace

from .ipc import BridgeError


class Column(list):
    def __eq__(self, value):
        return Mask(index for index, item in enumerate(self) if item == value)


class Mask(list):
    def nonzero(self):
        return (self,)


class Tiles:
    def __init__(self, columns):
        names = {'terrain', 'territory', 'city', 'owner'}
        if (not isinstance(columns, dict) or set(columns) != names or
                any(not isinstance(values, list) or len(values) != 220 * 220
                    for values in columns.values())):
            raise BridgeError('invalid_world', 'Invalid semantic map columns')
        self.columns = columns

    def __getitem__(self, key):
        if isinstance(key, str):
            return self.columns[key]
        return {name: values[key] for name, values in self.columns.items()}


class Snapshot(SimpleNamespace):
    def __init__(self, payload):
        if not isinstance(payload, dict) or payload.get('version') != 1:
            raise BridgeError('incompatible', 'Unsupported semantic snapshot')
        if not isinstance(payload.get('metadata'), dict) or not payload['metadata'].get('snapshot_id'):
            raise BridgeError('invalid_world', 'Missing semantic snapshot identity')
        super().__init__(**payload)
        for name in ('cities', 'forces', 'officers', 'bases'):
            if name not in payload:
                continue
            rows = payload[name]
            if (not isinstance(rows, list) or any(not isinstance(row, dict) or type(row.get('id')) is not int for row in rows)
                    or len({row['id'] for row in rows}) != len(rows)):
                raise BridgeError('invalid_world', f'Invalid semantic {name} identities')
            setattr(self, name, {row['id']: row for row in rows})
        for name in ('officer_names', 'force_names', 'order_names'):
            if name in payload:
                setattr(self, name, dict(payload[name]))
        if 'tiles' in payload:
            self.tiles = Tiles(payload['tiles'])
            if not isinstance(payload.get('tile_forces'), list) or len(payload['tile_forces']) != 220 * 220:
                raise BridgeError('invalid_world', 'Invalid semantic tile ownership')
            self.tile_forces = Column(payload['tile_forces'])

    def officer_name(self, identity):
        return self.officer_names.get(identity, f'#{identity}')

    def terrain_at(self, x, y):
        value = self.tiles['terrain'][tile_of(x, y)]
        names = self.terrain_names
        return names[value] if value < len(names) and names[value].strip() else f'地形{value}'


def tile_of(x, y):
    return y * 220 + x


def hex_distance(a, b):
    q1, r1 = a[0], a[1] - (a[0] + (a[0] & 1)) // 2
    q2, r2 = b[0], b[1] - (b[0] + (b[0] & 1)) // 2
    dq, dr = q2 - q1, r2 - r1
    return (abs(dq) + abs(dr) + abs(dq + dr)) // 2


def neighbors(x, y):
    if x % 2:
        candidates = [(x,y-1),(x,y+1),(x-1,y-1),(x-1,y),(x+1,y-1),(x+1,y)]
    else:
        candidates = [(x,y-1),(x,y+1),(x-1,y),(x-1,y+1),(x+1,y),(x+1,y+1)]
    return [(a,b) for a,b in candidates if 0 <= a < 220 and 0 <= b < 220]


class QueryContext:
    def __init__(self, rpc, name, authority):
        self.rpc, self.name, self.authority = rpc, name, authority
        self.snapshot = None

    def capture(self):
        if self.snapshot is None:
            self.snapshot = Snapshot(self.rpc.call('semantic.snapshot', {
                'version': 1, 'tool': self.name, 'authority': self.authority}, timeout=60))
        return self.snapshot


current_query = ContextVar('san14_semantic_query')


def snap():
    return current_query.get().capture()
