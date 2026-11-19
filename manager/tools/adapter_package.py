"""Validate the fixed binary-only GameAdapter runtime package."""
import hashlib
import json
from pathlib import Path

def validate_adapter(package: Path):
    base=Path(package)/'SAN14Base/GameAdapter'
    manifest=json.loads((base/'adapter.json').read_text('utf-8-sig'))
    if (manifest.get('schema_version')!=1 or manifest.get('abi_version')!=1 or
        manifest.get('sdk_abi_version')!=65536 or manifest.get('binary')!='GameAdapter.dll' or
        manifest.get('embedded_commands') is not True or not manifest.get('adapter_id') or not manifest.get('adapter_version')):
        raise ValueError('Incompatible GameAdapter manifest')
    binary=base/'GameAdapter.dll'
    if binary.is_symlink() or not binary.is_file() or hashlib.sha256(binary.read_bytes()).hexdigest()!=manifest.get('sha256'):
        raise ValueError('GameAdapter integrity mismatch')
    if (Path(package)/'SAN14Base/commands').exists():
        raise ValueError('Embedded GameAdapter package must not contain plaintext command assets')
    return manifest
