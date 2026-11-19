import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from zipfile import ZipFile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('san14_monorepo_package',ROOT/'tools/package.py')
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)


class PackagingTests(unittest.TestCase):
    def test_default_does_not_select_optional_mods(self):
        selected=package.selected(ROOT)
        self.assertEqual({item['id'] for item in selected},
                         {'companion-mod','save-stats-mod','ai-iq-mod','enhanced-font-mod','enhanced-settings-mod','mcp-mod'})
        self.assertTrue(all(item['group']=='default' for item in selected))

    def test_optional_selection_is_explicit(self):
        all_mods=package.selected(ROOT,optional=True)
        self.assertEqual({item['id'] for item in all_mods if item['group']=='optional'},
                         {'officer-temperament-mod','two-city-all-stars-mod'})
        single=package.selected(ROOT,['officer-temperament-mod'])
        self.assertEqual(len(single),1);self.assertEqual(single[0]['group'],'optional')
        with self.assertRaises(ValueError):package.selected(ROOT,['unknown-mod'])

    def test_package_paths_cannot_escape_the_mod(self):
        with tempfile.TemporaryDirectory() as temporary:
            root=Path(temporary);(root/'mod.json').write_text('{}')
            for name in ('../mod.json','/mod.json','C:/mod.json','content\\mod.json'):
                with self.subTest(name=name),self.assertRaises(ValueError):package.safe_file(root,name)
            self.assertEqual(package.safe_file(root,'mod.json'),root/'mod.json')

    def test_written_bundle_has_only_requested_payload(self):
        with tempfile.TemporaryDirectory() as temporary:
            path=Path(temporary)/'bundle.zip'
            expected={'SAN14Mods/mcp-mod/mod.json':b'{}','SAN14Mods/mcp-mod/bin/win-x64/mcp.dll':b'fixture'}
            package.write_zip(path,expected)
            with ZipFile(path) as archive:
                self.assertEqual({name:archive.read(name) for name in archive.namelist()},expected)
                self.assertFalse(any('/optional/' in name or name.endswith('.cpp') for name in archive.namelist()))


if __name__=='__main__':unittest.main()
