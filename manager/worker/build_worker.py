"""Freeze the semantic Worker and emit the manifest the Host build pins."""
import argparse
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import sys
import tempfile


ROOT = Path(__file__).resolve().parent
ENTRY = "san14-managed-worker"
INTEGRITY = "worker.integrity.json"


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def inventory(output):
    rows = []
    for path in sorted(output.rglob("*"), key=lambda value: value.relative_to(output).as_posix()):
        metadata = path.lstat()
        if path.is_symlink() or getattr(metadata, "st_file_attributes", 0) & stat.FILE_ATTRIBUTE_REPARSE_POINT:
            raise ValueError("Worker deployment contains a reparse point")
        if path.is_file() and path.name != INTEGRITY:
            rows.append({"path": path.relative_to(output).as_posix(), "size": path.stat().st_size,
                         "sha256": sha256(path)})
    return rows


def copy_notices(output, roots=("mcp", "uvicorn", "PyInstaller")):
    from packaging.requirements import Requirement

    pending = list(roots)
    seen = set()
    notices = ["Third-party notices for the SAN14 semantic MCP Worker", ""]
    while pending:
        name = pending.pop().lower().replace("_", "-")
        if name in seen:
            continue
        seen.add(name)
        package = importlib.metadata.distribution(name)
        label = f"{name}-{package.version}"
        notices.append(f"{label}: {package.metadata.get('License-Expression') or 'see package license'}")
        for item in package.files or ():
            relative = Path(item)
            if not any(fragment in relative.name.casefold() for fragment in ("license", "copying", "notice")):
                continue
            if relative.is_absolute() or ".." in relative.parts:
                raise ValueError("Unsafe license file path")
            source = package.locate_file(item)
            if source.is_file():
                destination = output / "licenses" / label / relative
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, destination)
        for requirement in package.requires or ():
            dependency = Requirement(requirement)
            if dependency.marker is None or dependency.marker.evaluate({"extra": ""}):
                pending.append(dependency.name)
    license_path = Path(sys.base_prefix) / "LICENSE.txt"
    if license_path.is_file():
        (output / "licenses").mkdir(exist_ok=True)
        shutil.copyfile(license_path, output / "licenses" / "Python-LICENSE.txt")
    (output / "THIRD-PARTY-NOTICES.txt").write_text("\n".join(notices) + "\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT.parent / "build" / "semantic-worker")
    args = parser.parse_args()
    output = args.output.resolve()
    if output.exists():
        parser.error("Choose a fresh output directory")
    for dependency, expected in (("PyInstaller", "6.22.3"), ("mcp", "2.3.0"), ("uvicorn", "0.54.0")):
        if importlib.metadata.version(dependency) != expected:
            parser.error(f"{dependency} {expected} is required")
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = Path(tempfile.mkdtemp(prefix=".freeze-", dir=output.parent))
    spec = staging / "semantic_worker.spec"
    spec.write_text(
        "from pathlib import Path\n"
        "from PyInstaller.utils.hooks import collect_all\n"
        "datas, binaries, imports = [], [], []\n"
        "for package in ('mcp', 'mcp_types', 'uvicorn'):\n"
        "    data, binary, hidden = collect_all(package, include_py_files=False,\n"
        "        filter_submodules=lambda name: not name.startswith('mcp.cli'))\n"
        "    datas.extend(data); binaries.extend(binary); imports.extend(hidden)\n"
        f"a = Analysis([{str(ROOT / 'launcher.py')!r}], pathex=[{str(ROOT)!r}], datas=datas,\n"
        "    binaries=binaries, hiddenimports=imports, excludes=['san14', 'frida', '_frida', 'numpy'])\n"
        "a.datas = [entry for entry in a.datas if Path(entry[0]).name.lower() != 'direct_url.json']\n"
        "pyz = PYZ(a.pure)\n"
        f"exe = EXE(pyz, a.scripts, [], name={ENTRY!r}, exclude_binaries=True, console=False, upx=False)\n"
        f"coll = COLLECT(exe, a.binaries, a.datas, name={output.name!r}, upx=False)\n",
        encoding="utf-8",
    )
    subprocess.run([sys.executable, "-m", "PyInstaller", "--noconfirm", "--log-level", "WARN",
                    "--distpath", str(output.parent), "--workpath", str(staging), str(spec)], check=True)
    executable = output / (ENTRY + ".exe")
    subprocess.run([str(executable), "--help"], cwd=output, check=True, timeout=30,
                   creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    copy_notices(output)
    (output / 'build-manifest.json').write_text(json.dumps({
        'schema_version': 1, 'entrypoint': executable.name, 'runtime_kind': 'semantic_worker',
        'managed_only': True, 'external_frida_runtime': False, 'console_window': False,
        'http_enabled_by_default': False, 'exe_sha256': sha256(executable),
        'sources': {path.relative_to(ROOT).as_posix(): sha256(path)
                    for path in sorted((ROOT / 'san14_worker').glob('*.py'))},
    }, sort_keys=True, indent=2) + '\n', encoding='utf-8', newline='\n')
    rows = inventory(output)
    if any(part.casefold() in {"direct_url.json", "san14", "native_profiles"} or "frida" in part.casefold()
           for row in rows for part in Path(row["path"]).parts):
        raise ValueError("Unexpected private game dependency in Worker deployment")
    manifest = output / INTEGRITY
    manifest.write_text(json.dumps({"schema_version": 1, "entrypoint": executable.name, "files": rows},
                                   ensure_ascii=False, sort_keys=True, indent=2) + "\n",
                        encoding="utf-8", newline="\n")
    print(manifest)


if __name__ == "__main__":
    main()
