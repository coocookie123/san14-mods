"""Pack a previously built, verified runtime into a local GameAdapter resource."""
import hashlib
import json
from pathlib import Path
import re
import stat
import sys
from zipfile import ZIP_DEFLATED, ZipFile


def main():
    directory, pin, archive = map(Path, sys.argv[1:])
    manifest = directory / "adapter.runtime.json"
    data = manifest.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if not re.search(rf'\bmanifest_sha256\[\]\s*=\s*"{digest}"', pin.read_text("utf-8")):
        raise ValueError("Adapter runtime bundle differs from GameAdapter build inputs")
    integrity = json.loads(data)
    if (integrity.get("schema_version") != 1 or integrity.get("entrypoint") != "san14.adapter_service" or
            integrity.get("kind") != "in_process"):
        raise ValueError("Invalid adapter runtime manifest")
    found = {}
    for path in directory.rglob("*"):
        metadata = path.lstat()
        if path.is_symlink() or getattr(metadata, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError("Adapter runtime assets cannot contain reparse points")
        if path.is_file() and path != manifest:
            with path.open("rb") as stream:
                hash_value = hashlib.file_digest(stream, "sha256").hexdigest()
            name = path.relative_to(directory).as_posix()
            found[name] = {"path": name, "size": metadata.st_size, "sha256": hash_value}
    expected = {row["path"]: row for row in integrity["files"]}
    if any(name.lower().endswith('.exe') for name in found):
        raise ValueError('A GameAdapter runtime must not contain a backend executable')
    if (found != expected or len(expected) != len(integrity["files"]) or
            len(found) != len({name.casefold() for name in found})):
        raise ValueError("Adapter runtime directory is incomplete or changed")
    with ZipFile(archive, "w", ZIP_DEFLATED) as payload:
        payload.write(manifest, manifest.name)
        for name in sorted(found):
            payload.write(directory / name, name)
    with ZipFile(archive) as payload:
        if payload.testzip() is not None:
            raise ValueError("Adapter runtime resource CRC failed")


if __name__ == "__main__":
    main()
