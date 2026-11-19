from hashlib import sha256
from io import BytesIO
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
from zipfile import ZipFile, ZipInfo

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fetch_adapter',ROOT/'tools/fetch_adapter.py')
fetch=importlib.util.module_from_spec(spec);spec.loader.exec_module(fetch)


class FetchAdapterTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name);(self.root/'manager').mkdir()
        self.output=self.root/'build/first/adapter-package'
        binary=b'MZ-adapter-fixture'
        self.manifest={'schema_version':1,'abi_version':1,'sdk_abi_version':65536,
            'adapter_id':'fixture','adapter_version':'0.1.0','binary':'GameAdapter.dll',
            'sha256':sha256(binary).hexdigest(),'embedded_commands':True}
        self.files={'SAN14Base/GameAdapter/GameAdapter.dll':binary,
            'SAN14Base/GameAdapter/adapter.json':json.dumps(self.manifest).encode(),
            'README.txt':b'fixture','THIRD-PARTY-NOTICES.txt':b'notices','licenses/LICENSE':b'license'}
        self.write_archive()

    def write_archive(self):
        buffer=BytesIO()
        with ZipFile(buffer,'w') as archive:
            for name,data in self.files.items():archive.writestr(name,data)
        self.data=buffer.getvalue()
        self.lock={'schema_version':1,'component':'GameAdapter','version':'0.1.0','tag':'fixture',
            'url':'https://github.com/zhaoqxu/san14-mods/releases/download/fixture/fixture.zip',
            'sha256':sha256(self.data).hexdigest(),'archive_bytes':len(self.data),
            'adapter_sha256':self.manifest['sha256'],'adapter_id':'fixture','adapter_abi_version':1,'sdk_abi_version':65536}
        (self.root/'manager/adapter.lock.json').write_text(json.dumps(self.lock))

    def test_first_download_then_offline_cache_and_output_repair(self):
        with patch.object(fetch,'open_download',return_value=BytesIO(self.data)) as network:
            first=fetch.ensure_adapter(self.root,self.output)
            self.assertTrue(first['downloaded']);network.assert_called_once_with(self.lock['url'])
        with patch.object(fetch,'open_download',side_effect=AssertionError('offline network call')):
            second=fetch.ensure_adapter(self.root,self.output,offline=True)
            self.assertEqual(second['replaced_files'],0)
            (self.output/'SAN14Base/GameAdapter/GameAdapter.dll').write_bytes(b'tampered')
            third=fetch.ensure_adapter(self.root,self.output,offline=True)
            self.assertEqual(third['replaced_files'],1)
        self.assertEqual((self.output/'SAN14Base/GameAdapter/GameAdapter.dll').read_bytes(),self.files['SAN14Base/GameAdapter/GameAdapter.dll'])

    def test_wrong_download_checksum_never_extracts_or_caches(self):
        with patch.object(fetch,'open_download',return_value=BytesIO(b'x'*len(self.data))):
            with self.assertRaisesRegex(fetch.AdapterError,'SHA-256'):fetch.ensure_adapter(self.root,self.output)
        self.assertFalse(self.output.exists())
        self.assertEqual(list((self.root/'build/.cache/gameadapter').iterdir()),[])

    def test_corrupt_cache_is_rejected_without_network(self):
        cache=self.root/'build/.cache/gameadapter'/(self.lock['sha256']+'.zip')
        cache.parent.mkdir(parents=True);cache.write_bytes(b'corrupt')
        with patch.object(fetch,'open_download',side_effect=AssertionError('network must not run')):
            with self.assertRaisesRegex(fetch.AdapterError,'SHA-256'):fetch.ensure_adapter(self.root,self.output)

    def test_offline_missing_cache_and_verified_local_archive(self):
        with self.assertRaisesRegex(fetch.AdapterError,'offline mode'):fetch.ensure_adapter(self.root,self.output,offline=True)
        path=self.root/'supplied.zip';path.write_bytes(self.data)
        with patch.object(fetch,'open_download',side_effect=AssertionError('local archive should work offline')):
            result=fetch.ensure_adapter(self.root,self.output,offline=True,archive_path=path)
        self.assertFalse(result['downloaded']);self.assertEqual(result['files'],5)

    def test_unexpected_user_file_is_preserved(self):
        self.output.mkdir(parents=True);user=self.output/'user.txt';user.write_text('keep')
        with patch.object(fetch,'open_download',return_value=BytesIO(self.data)):
            with self.assertRaisesRegex(fetch.AdapterError,'unexpected files'):fetch.ensure_adapter(self.root,self.output)
        self.assertEqual(user.read_text(),'keep')

    def test_archive_cannot_escape_or_collide_on_windows(self):
        for name in ('../outside.txt','C:/outside.txt','licenses\\outside.txt','licenses/CON.txt','licenses/file:stream','licenses/file.','readme.TXT'):
            with self.subTest(name=name):
                buffer=BytesIO()
                with ZipFile(buffer,'w') as archive:
                    for existing,data in self.files.items():archive.writestr(existing,data)
                    entry=ZipInfo();entry.filename=name
                    archive.writestr(entry,b'bad')
                with ZipFile(BytesIO(buffer.getvalue())) as archive:
                    with self.assertRaises(fetch.AdapterError):fetch.archive_files(archive)

    def test_output_cannot_write_outside_build(self):
        with self.assertRaisesRegex(fetch.AdapterError,'under this checkout build'):
            fetch.ensure_adapter(self.root,self.root/'outside')

    def test_manifest_abi_mismatch_is_rejected(self):
        manifest=dict(self.manifest,abi_version=9)
        self.files['SAN14Base/GameAdapter/adapter.json']=json.dumps(manifest).encode();self.write_archive()
        with patch.object(fetch,'open_download',return_value=BytesIO(self.data)):
            with self.assertRaisesRegex(fetch.AdapterError,'ABI'):fetch.ensure_adapter(self.root,self.output)
        self.assertFalse(self.output.exists())


if __name__=='__main__':unittest.main()
