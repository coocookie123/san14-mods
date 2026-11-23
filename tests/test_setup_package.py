import hashlib
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from zipfile import ZipFile


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from package import read, selected
from package_setup import FOOTER, PAYLOAD_MAGIC, package


class SetupPackageTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.output = Path(temporary.name)
        self.stub = b"MZ fixture"
        (self.output / "native").mkdir()
        (self.output / "native/san14-setup-stub.exe").write_bytes(self.stub)
        self.version = read(ROOT / "manager/project.json")["version"]
        self.runtime = {
            "dinput8.dll": b"host",
            "package.json": json.dumps({"version": self.version}).encode(),
            "SAN14Base/GameAdapter/adapter.json": b"adapter manifest",
            "SAN14Base/GameAdapter/GameAdapter.dll": b"adapter",
            "SAN14Base/GameAdapter/licenses/example/LICENSE": b"adapter license",
            "SAN14Base/worker/san14-managed-worker.exe": b"worker",
            "SAN14Base/worker/worker.integrity.json": b"worker manifest",
        }
        self.write_runtime()
        (self.output / "mods/default").mkdir(parents=True)
        self.mod_packages = {}
        self.records = [{"id": "optional-fixture", "group": "optional"}]
        for mod in selected(ROOT):
            mod_id = mod["id"]
            manifest = ROOT / mod["path"] / "mod.json"
            filename = f"{mod_id}-{mod['version']}.s14mod"
            path = self.output / "mods/default" / filename
            with ZipFile(path, "w") as archive:
                archive.writestr("mod.json", manifest.read_bytes())
                archive.writestr(read(manifest)["entry"]["path"], b"mod")
            data = path.read_bytes()
            self.mod_packages[f"SAN14ModPackages/{mod_id}/{filename}"] = data
            self.records.append({"id": mod_id, "group": "default", "sha256": hashlib.sha256(data).hexdigest()})
        self.write_report()

    def write_runtime(self):
        with ZipFile(self.output / "SAN14-runtime.zip", "w") as archive:
            for name, data in self.runtime.items():
                archive.writestr(name, data)

    def write_report(self):
        (self.output / "packages.json").write_text(json.dumps({"mods": self.records}), encoding="utf-8")

    def test_embeds_only_the_default_mods_and_runtime(self):
        report = package(ROOT, self.output)
        self.assertEqual(report["default_mods"], 7)
        output = Path(report["file"])
        self.assertEqual(output.name, f"SAN14-ModManager-Setup-{self.version}.exe")
        exe = output.read_bytes()
        self.assertEqual(report["sha256"], hashlib.sha256(exe).hexdigest())
        magic, offset, size, sha = FOOTER.unpack(exe[-FOOTER.size:])
        payload = exe[offset:-FOOTER.size]
        self.assertEqual((magic, offset, size, sha), (
            PAYLOAD_MAGIC, len(self.stub), len(payload), hashlib.sha256(payload).digest()))
        self.assertEqual(exe[:offset], self.stub)
        with ZipFile(io.BytesIO(payload)) as archive:
            self.assertEqual({name: archive.read(name) for name in archive.namelist()},
                             self.runtime | self.mod_packages)

    def test_rejects_missing_default_mod(self):
        self.records.pop()
        self.write_report()
        with self.assertRaisesRegex(ValueError, "complete default Mod group"):
            package(ROOT, self.output)
        self.assertEqual(list(self.output.glob("*.exe")), [])

    def test_rejects_changed_mod_archive(self):
        next((self.output / "mods/default").glob("*.s14mod")).write_bytes(b"changed")
        with self.assertRaisesRegex(ValueError, "changed after packaging"):
            package(ROOT, self.output)

    def test_rejects_runtime_path_escape(self):
        self.runtime["SAN14Base/../outside"] = b"unsafe"
        self.write_runtime()
        with self.assertRaisesRegex(ValueError, "Unsafe or duplicate"):
            package(ROOT, self.output)


if __name__ == "__main__":
    unittest.main()
