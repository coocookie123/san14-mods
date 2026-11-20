"""Embed the verified runtime and default Mod packages in the Windows Setup stub."""
import argparse
import hashlib
import io
from importlib.metadata import distribution
import json
from pathlib import Path
import re
import stat
import struct
import subprocess
import sys
import tempfile
from zipfile import ZIP_DEFLATED, ZipFile

from package import read, selected


FOOTER = struct.Struct("<8sQQ32s")
PAYLOAD_MAGIC = b"S14PKG01"
MAX_PAYLOAD = 512 * 1024 * 1024
DEVICE_NAME = re.compile(r"^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(\.|$)", re.I)


def entries(archive):
    names = set()
    for entry in archive.infolist():
        name = entry.filename
        if (entry.orig_filename != name or "\\" in name or ":" in name or
                any(not part or part in (".", "..") or part.endswith((" ", ".")) or
                    DEVICE_NAME.match(part) for part in name.split("/")) or
                entry.is_dir() or stat.S_ISLNK(entry.external_attr >> 16) or name.casefold() in names):
            raise ValueError(f"Unsafe or duplicate Setup archive entry: {name}")
        names.add(name.casefold())
        yield name


def bundle_two_city(payload, root, artifacts, report):
    mod_id = "two-city-all-stars-mod"
    project = root / "mods/optional" / mod_id
    source_manifest = read(project / "mod.json")
    filename = f"{mod_id}-{source_manifest['version']}.s14mod"
    record = next((item for item in report if item["id"] == mod_id and item["group"] == "optional"), None)
    if record is None:
        raise ValueError("Build the optional Two City Mod before bundling it in Setup")
    data = (artifacts / "mods/optional" / filename).read_bytes()
    if hashlib.sha256(data).hexdigest() != record["sha256"]:
        raise ValueError("Two City Mod package changed after packaging")
    definition_file = "__san14_content__/definition.json"
    with ZipFile(io.BytesIO(data)) as archive:
        names = set(entries(archive))
        metadata = {name: archive.read(name) for name in ("mod.json", "README.md", definition_file)}
        compiled_manifest = dict(source_manifest, entry={"type": "content", "path": definition_file})
        definition = json.loads(metadata[definition_file])
        native = definition["scenario"]["native_file"]
        content_hash = native["sha256"]
        native_file = f"__san14_content__/{content_hash}.s14"
        if (json.loads(metadata["mod.json"]) != compiled_manifest or
                definition["id"] != mod_id or definition["version"] != source_manifest["version"] or
                definition.get("compiled") is not True or not re.fullmatch(r"[0-9a-f]{64}", content_hash) or
                native["path"] != native_file or names != set(metadata) | {native_file} or
                hashlib.sha256(archive.read(native_file)).hexdigest() != content_hash):
            raise ValueError("Invalid compiled Two City Mod package")
    with tempfile.TemporaryDirectory(prefix="two-city-", dir=artifacts) as directory:
        staging = Path(directory)
        subprocess.run([
            sys.executable, "-m", "PyInstaller", "--noconfirm", "--noupx",
            "--log-level", "WARN", "--name", "author", "--distpath", str(staging / "dist"),
            "--workpath", str(staging / "work"), "--specpath", str(staging),
            str(project / "tools/build_duel_scenario.py"),
        ], cwd=staging, check=True)
        author = staging / "dist/author"
        if not (author / "author.exe").is_file():
            raise ValueError("Missing frozen Two City author")
        prefix = "SAN14TwoCity/"
        for path in sorted(author.rglob("*")):
            if path.is_file():
                payload.write(path, prefix + "runtime/" + path.relative_to(author).as_posix())
        for name, contents in metadata.items():
            payload.writestr(prefix + name, contents)
        pyinstaller = distribution("PyInstaller")
        license_file = next(path for path in pyinstaller.files if path.name == "COPYING.txt")
        payload.writestr(prefix + "licenses/PyInstaller-COPYING.txt", license_file.locate().read_bytes())
        payload.writestr(prefix + "licenses/Python-LICENSE.txt", (Path(sys.base_prefix) / "LICENSE.txt").read_bytes())


def package(root, artifacts, include_two_city=False):
    version = read(root / "manager/project.json")["version"]
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        raise ValueError("Invalid manager installer version")
    stub = (artifacts / "native/san14-setup-stub.exe").read_bytes()
    if not stub.startswith(b"MZ"):
        raise ValueError("Expected a Windows Setup stub")

    defaults = selected(root)
    report = read(artifacts / "packages.json")["mods"]
    records = {item["id"]: item for item in report if item["group"] == "default"}
    if len(records) != sum(item["group"] == "default" for item in report) or set(records) != {
        item["id"] for item in defaults
    }:
        raise ValueError("Setup requires the complete default Mod group")

    buffer = io.BytesIO()
    with ZipFile(buffer, "w", ZIP_DEFLATED) as payload:
        with ZipFile(artifacts / "SAN14-runtime.zip") as runtime:
            names = set(entries(runtime))
            required = {
                "dinput8.dll", "package.json", "SAN14Base/GameAdapter/adapter.json",
                "SAN14Base/GameAdapter/GameAdapter.dll", "SAN14Base/worker/san14-managed-worker.exe",
                "SAN14Base/worker/worker.integrity.json",
            }
            if not required <= names or any(
                name not in ("dinput8.dll", "package.json") and not name.startswith("SAN14Base/") for name in names
            ):
                raise ValueError("Setup requires a complete manager runtime")
            if json.loads(runtime.read("package.json"))["version"] != version:
                raise ValueError("Manager runtime version does not match Setup")
            for name in sorted(names):
                payload.writestr(name, runtime.read(name))

        for mod in defaults:
            mod_id = mod["id"]
            filename = f"{mod_id}-{mod['version']}.s14mod"
            data = (artifacts / "mods/default" / filename).read_bytes()
            if hashlib.sha256(data).hexdigest() != records[mod_id]["sha256"]:
                raise ValueError(f"Default Mod package changed after packaging: {mod_id}")
            with ZipFile(io.BytesIO(data)) as archive:
                names = set(entries(archive))
                manifest = json.loads(archive.read("mod.json"))
                if (manifest != read(root / mod["path"] / "mod.json") or
                        manifest["entry"]["path"] not in names or archive.testzip() is not None):
                    raise ValueError(f"Invalid default Mod package: {mod_id}")
            payload.writestr(f"SAN14ModPackages/{mod_id}/{filename}", data)

        if include_two_city:
            bundle_two_city(payload, root, artifacts, report)

    with ZipFile(buffer) as payload:
        if payload.testzip() is not None:
            raise ValueError("Setup payload CRC verification failed")
    data = buffer.getbuffer()
    if not 0 < len(data) <= MAX_PAYLOAD:
        raise ValueError("Setup payload exceeds the native extractor limit")
    footer = FOOTER.pack(PAYLOAD_MAGIC, len(stub), len(data), hashlib.sha256(data).digest())
    output = artifacts / f"SAN14-ModManager-Setup-{version}.exe"
    with tempfile.NamedTemporaryFile(dir=artifacts, prefix=output.name + ".", suffix=".tmp",
                                     delete_on_close=False) as temporary:
        temporary.write(stub)
        temporary.write(data)
        temporary.write(footer)
        temporary.close()
        Path(temporary.name).replace(output)
    with output.open("rb") as stream:
        checksum = hashlib.file_digest(stream, "sha256").hexdigest()
    return {"file": str(output), "sha256": checksum, "default_mods": len(defaults)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--artifact-root", required=True, type=Path)
    parser.add_argument("--bundle-two-city", action="store_true",
                        help="Bundle local generation using a prebuilt optional Two City Mod")
    args = parser.parse_args()
    root = args.root.resolve()
    artifacts = args.artifact_root.resolve()
    if artifacts == root / "build" or not artifacts.is_relative_to(root / "build"):
        parser.error("ArtifactRoot must be under this checkout build/")
    print(json.dumps(package(root, artifacts, include_two_city=args.bundle_two_city)))
