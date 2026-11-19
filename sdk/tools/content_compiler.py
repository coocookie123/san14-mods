"""Compile declarative SAN14 content and PNG portraits into a self-contained pack.

Authoring uses JSON and PNG. Runtime images are native PC G1T/BC3 textures;
the game never needs Pillow, an image converter, or the original author folder.
"""
from __future__ import annotations
import argparse
import hashlib
import io
import json
import math
from pathlib import Path, PurePosixPath
import struct
import zipfile

PREFIX = '__san14_content__/'
ENTRY = PREFIX + 'definition.json'
MAX_JSON = 2 * 1024 * 1024
MAX_ASSETS = 128 * 1024 * 1024


def encoded(value):
    return (json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n').encode('utf-8')


def safe_file(root: Path, relative: str) -> Path:
    if not isinstance(relative, str) or not relative or len(relative) > 220 or '\\' in relative or ':' in relative or '\0' in relative:
        raise ValueError('Invalid content relative path')
    parts = PurePosixPath(relative).parts
    if relative.startswith('/') or any(p in ('', '.', '..') or p.endswith((' ', '.')) for p in relative.split('/')):
        raise ValueError('Content path traversal refused')
    current = root
    for part in parts:
        current /= part
        if current.is_symlink() or (current.exists() and getattr(current.stat(), 'st_file_attributes', 0) & 0x400):
            raise ValueError('Content reparse points refused')
    if not current.is_file() or not current.resolve().is_relative_to(root.resolve()):
        raise ValueError('Missing content file: ' + relative)
    return current


def load_json(root, path):
    raw = safe_file(root, path).read_bytes()
    if not raw or len(raw) > MAX_JSON:
        raise ValueError('Content JSON exceeds 2 MiB')
    def pairs(values):
        out = {}
        for key, value in values:
            if key in out:
                raise ValueError('Duplicate JSON property: ' + key)
            out[key] = value
        return out
    return json.loads(raw, object_pairs_hook=pairs)


def identifier(value):
    return isinstance(value, str) and 1 <= len(value) <= 64 and all(c in 'abcdefghijklmnopqrstuvwxyz0123456789._-' for c in value)


def g1t_from_png(raw: bytes):
    from PIL import Image
    with Image.open(io.BytesIO(raw)) as source:
        if source.format != 'PNG' or source.width < 1 or source.height < 1 or max(source.size) > 2048 or source.width * source.height > 2048 * 2048:
            raise ValueError('Portraits require PNG dimensions 1..2048')
        image = source.convert('RGBA')
    width, height = image.size
    padded_w = max(4, 1 << (width - 1).bit_length())
    padded_h = max(4, 1 << (height - 1).bit_length())
    padded = Image.new('RGBA', (padded_w, padded_h), (0, 0, 0, 0))
    padded.paste(image, (0, 0))
    dds = io.BytesIO()
    padded.save(dds, format='DDS', pixel_format='DXT5')
    block = dds.getvalue()[128:]
    assert len(block) == padded_w * padded_h
    texture = bytes((0x10, 0x5b, int(math.log2(padded_w)) | (int(math.log2(padded_h)) << 4), 0, 0, 0, 0, 0)) + block
    header = struct.pack('<8I', 0x47315447, 0x30303630, 36 + len(texture), 32, 1, 10, 0, 0)
    result = header + struct.pack('<I', 4) + texture
    return result, [0, 0, width / padded_w, 0, 0, height / padded_h, width / padded_w, height / padded_h]


def validate_definition(data):
    import jsonschema
    schema = json.loads((Path(__file__).resolve().parents[1] / 'schemas/content.schema.json').read_text('utf-8'))
    jsonschema.validate(data, schema)
    allowed = {'schema_version', 'compiled', 'id', 'version', 'scenario', 'officers', 'traits', 'cities', 'posts', 'formations', 'resources', 'officer_stat_cap', 'campaign'}
    if not isinstance(data, dict) or set(data) - allowed or data.get('schema_version') != 2:
        raise ValueError('Expected declarative content schema_version 2')
    if not identifier(data.get('id')) or not isinstance(data.get('version'), str):
        raise ValueError('Invalid content identity')
    scenario = data.get('scenario')
    if not isinstance(scenario, dict) or set(scenario) - {'key', 'base_id', 'name', 'description', 'native_file'} or not {'key', 'base_id', 'name', 'description'} <= set(scenario) or not identifier(scenario['key']):
        raise ValueError('Scenario requires key, base_id, name and description')
    if type(scenario['base_id']) is not int or not 0 <= scenario['base_id'] <= 199:
        raise ValueError('Invalid native scenario base ID')
    if 'native_file' in scenario:
        if any(data.get(t) for t in ('officers', 'traits', 'cities', 'posts', 'formations', 'resources')):
            raise ValueError('Native scenario files cannot mix runtime record edits')
        if data.get('compiled') is True and (not isinstance(scenario['native_file'], dict) or set(scenario['native_file']) != {'path', 'sha256'}):
            raise ValueError('Compiled native scenario requires path and sha256')
    if 'campaign' in data:
        sides = data['campaign']['sides']
        roster = [name for side in sides for name in side['officers']]
        if len(set(roster)) != len(roster) or sides[0]['city'] == sides[1]['city']:
            raise ValueError('Campaign rosters and capitals must be distinct')
        if len(sides[0]['officers']) != len(sides[1]['officers']):
            raise ValueError('Duel rosters must have equal sizes')
        default_title = data['campaign'].get('ruler_title', 'none')
        for side in sides:
            if side['ruler'] not in side['officers'] or side['advisor'] not in side['officers']:
                raise ValueError('Ruler and advisor must belong to their roster')
            if 'regime' in side and side.get('title', default_title) != 'emperor':
                raise ValueError('Imperial regime requires emperor title')
            for name in side['officers']:
                if len(name.encode('utf-16-le')) > 32 or any(ord(c) < 32 for c in name):
                    raise ValueError('Invalid campaign officer name')
    for value, limit in ((scenario['name'], 8), (scenario['description'], 200)):
        if not isinstance(value, str) or not value or len(value.encode('utf-16-le')) > limit * 2 or any(ord(c) < 32 for c in value):
            raise ValueError('Scenario caption exceeds native capacity')
    for table, limit in (('officers', 1850), ('traits', 355), ('cities', 51), ('posts', 72), ('formations', 14), ('resources', 512)):
        values = data.get(table, [])
        if not isinstance(values, list) or len(values) > limit:
            raise ValueError('Invalid content table: ' + table)
        seen = set()
        for row in values:
            if not isinstance(row, dict):
                raise ValueError('Content records must be objects')
            key = row.get('key', row.get('id'))
            if key is None or isinstance(key, bool) or key in seen:
                raise ValueError('Missing/duplicate content record identity: ' + table)
            if isinstance(key, str) and not identifier(key):
                raise ValueError('Invalid local record key')
            seen.add(key)
            limits = {'officers': {'surname': 8, 'given': 8, 'courtesy': 8}, 'traits': {'name': 4}, 'cities': {'name': 3}, 'posts': {'name': 4}}.get(table, {})
            for field, maximum in limits.items():
                if field in row and (len(row[field].encode('utf-16-le')) > maximum * 2 or any(ord(c) < 32 for c in row[field])):
                    raise ValueError('Content caption exceeds native capacity: ' + table + '.' + field)


def compile_package(root: Path, entry: str, mod_id: str, version: str):
    data = load_json(root, entry)
    # Tables may live in separate author files. They are expanded once at build
    # time; runtime does not chase file references or parse per frame.
    for table in ('officers', 'traits', 'cities', 'posts', 'formations', 'resources'):
        if isinstance(data.get(table), str):
            data[table] = load_json(root, data[table])
    validate_definition(data)
    if data['id'] != mod_id or data['version'] != version:
        raise ValueError('Content identity does not match mod.json')
    stat_max = max([100] + [max(row['stats']) for row in data.get('officers', []) if 'stats' in row])
    if data.get('officer_stat_cap', stat_max) < stat_max:
        raise ValueError('officer_stat_cap is lower than declared officer stats')
    if stat_max > 100 and 'officer_stat_cap' not in data:
        data['officer_stat_cap'] = stat_max
    generated = {}
    total = 0
    if 'native_file' in data['scenario']:
        if any(data.get(t) for t in ('officers', 'traits', 'cities', 'posts', 'formations', 'resources')):
            raise ValueError('Native scenario files cannot mix runtime record edits')
        source = data['scenario']['native_file']
        if not isinstance(source, str):
            raise ValueError('Author scenario.native_file must be a relative .s14 path')
        blob = safe_file(root, source).read_bytes()
        validate_native_scenario(blob)
        fingerprint = hashlib.sha256(blob).hexdigest()
        path = PREFIX + fingerprint + '.s14'
        generated[path] = blob
        data['scenario']['native_file'] = dict(path=path, sha256=fingerprint)
        total += len(blob)
    for asset in data.get('resources', []):
        if set(asset) != {'key', 'role', 'source'} or asset['role'] != 'portrait':
            raise ValueError('Resource requires key, role=portrait and PNG source')
        source = safe_file(root, asset['source'])
        if source.stat().st_size > 16 * 1024 * 1024:
            raise ValueError('Portrait source exceeds 16 MiB')
        raw = source.read_bytes()
        blob, uv = g1t_from_png(raw)
        fingerprint = hashlib.sha256(blob).hexdigest()
        path = PREFIX + fingerprint + '.g1t'
        if path not in generated:
            generated[path] = blob
            total += len(blob)
        if total > MAX_ASSETS:
            raise ValueError('Compiled resources exceed 128 MiB')
        key = asset['key']
        asset.clear()
        # Preserve the logical key independently from content-addressed files.
        asset.update(key=key, path=path, role='portrait', sha256=fingerprint, uv=uv)
    known_portraits = {r['key'] for r in data.get('resources', [])}
    known_traits = {r['key'] for r in data.get('traits', []) if 'key' in r}
    for row in data.get('officers', []):
        portrait = row.get('portrait')
        if portrait is not None and not (isinstance(portrait, str) and (portrait in known_portraits or portrait.startswith('builtin:'))):
            raise ValueError('Unknown officer portrait reference')
        for trait in row.get('traits', []):
            if isinstance(trait, str) and trait not in known_traits:
                raise ValueError('Unknown officer trait reference')
    data['compiled'] = True
    generated[ENTRY] = encoded(data)
    if len(generated[ENTRY]) > MAX_JSON:
        raise ValueError('Expanded content JSON exceeds 2 MiB')
    return generated


def verify_compiled(root: Path, entry: str, mod_id: str, version: str):
    data = load_json(root, entry)
    validate_definition(data)
    if entry != ENTRY or data.get('compiled') is not True or data['id'] != mod_id or data['version'] != version:
        raise ValueError('Invalid compiled content entry')
    total = 0
    if 'native_file' in data['scenario']:
        asset = data['scenario']['native_file']
        blob = safe_file(root, asset['path']).read_bytes()
        validate_native_scenario(blob)
        if hashlib.sha256(blob).hexdigest() != asset['sha256']:
            raise ValueError('Native scenario fingerprint mismatch')
        total += len(blob)
    for asset in data.get('resources', []):
        blob = safe_file(root, asset['path']).read_bytes()
        total += len(blob)
        if len(blob) > 4 * 1024 * 1024 + 44 or total > MAX_ASSETS or hashlib.sha256(blob).hexdigest() != asset['sha256']:
            raise ValueError('Compiled resource fingerprint/size mismatch')
        if len(blob) < 60 or blob[:8] != b'GT1G0600' or struct.unpack_from('<I', blob, 8)[0] != len(blob):
            raise ValueError('Invalid compiled G1T')
    return data


def validate_native_scenario(blob):
    if not 1022 <= len(blob) <= 16 * 1024 * 1024 or not blob.startswith(b'\x66\0\0\0SN14SCEXVER0001\0'):
        raise ValueError('Native scenario requires CE SC v102 .s14 format')
    magic = struct.pack('<I', 440620876)
    at = blob.find(magic)
    if at < 18 or at + 268 > len(blob):
        raise ValueError('Missing native scenario LWC payload')
    size, packed = struct.unpack_from('<II', blob, at + 4)
    if not 0 < size <= 32 * 1024 * 1024 or not 0 < packed or at + 268 + packed > len(blob) or len(set(blob[at + 12:at + 268])) != 256:
        raise ValueError('Invalid native scenario LWC framing')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mod_dir', type=Path)
    parser.add_argument('-o', '--output', type=Path, required=True)
    args = parser.parse_args()
    if args.output.suffix.lower() != '.s14mod':
        raise ValueError('Output must use .s14mod extension')
    # Portable SDK command. The repository packer applies the same compiler
    # after its usual stricter manifest and archive admission checks.
    import jsonschema
    root = args.mod_dir.resolve(strict=True)
    manifest = load_json(root, 'mod.json')
    schema = json.loads((Path(__file__).resolve().parents[1] / 'schemas/mod.schema.json').read_text('utf-8'))
    jsonschema.validate(manifest, schema)
    if manifest['entry']['type'] != 'content':
        raise ValueError('This compiler packages declarative content only')
    compiled = manifest['entry']['path'] == ENTRY
    if compiled:
        verify_compiled(root, ENTRY, manifest['id'], manifest['version'])
        generated = {}
    else:
        generated = compile_package(root, manifest['entry']['path'], manifest['id'], manifest['version'])
    manifest['entry'] = {'type': 'content', 'path': ENTRY}
    generated['mod.json'] = encoded(manifest)
    for source in root.rglob('*'):
        if source.is_symlink():
            raise ValueError('Package symlinks refused')
        if not source.is_file():
            continue
        relative = source.relative_to(root).as_posix()
        if relative.startswith(PREFIX) and not compiled:
            raise ValueError('Compiler namespace is reserved')
        if source.name in ('project.json', 'build.ps1', 'CMakeLists.txt', '.gitignore', '.gitmodules') or source.suffix.lower() in ('.cpp', '.h', '.pdb', '.obj', '.s14mod') or any(x in ('build', 'dist', '.git', '__pycache__', 'src', 'tests', 'tools') for x in source.relative_to(root).parts):
            continue
        safe_file(root, relative)
        if relative != 'mod.json':
            generated[relative] = source.read_bytes()
    if len(generated) > 2048 or sum(map(len, generated.values())) > 256 * 1024 * 1024 or any(len(b) > 64 * 1024 * 1024 for b in generated.values()):
        raise ValueError('Content package exceeds archive limits')
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temp = args.output.with_name(args.output.name + '.tmp')
    try:
        with zipfile.ZipFile(temp, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, blob in sorted(generated.items()):
                archive.writestr(name, blob)
        if temp.stat().st_size > 128 * 1024 * 1024:
            raise ValueError('Compressed content package exceeds 128 MiB')
        temp.replace(args.output)
    finally:
        temp.unlink(missing_ok=True)
    print(args.output)


if __name__ == '__main__':
    main()
