"""Fetch the pinned public GameAdapter without access to its private source repo."""
import argparse
from hashlib import sha256
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import ssl
import stat
import sys
import tempfile
from urllib.request import Request, urlopen
from zipfile import BadZipFile, ZipFile


class AdapterError(ValueError):
    pass


def digest(path):
    result=sha256()
    with path.open('rb') as stream:
        for block in iter(lambda:stream.read(1024*1024),b''):result.update(block)
    return result.hexdigest()


def read_lock(root):
    lock=json.loads((root/'manager/adapter.lock.json').read_text('utf-8-sig'))
    if (lock.get('schema_version')!=1 or lock.get('component')!='GameAdapter' or
        not re.fullmatch(r'[0-9a-f]{64}',lock.get('sha256','')) or
        not re.fullmatch(r'[0-9a-f]{64}',lock.get('adapter_sha256','')) or
        not isinstance(lock.get('archive_bytes'),int) or not 0<lock['archive_bytes']<=512*1024*1024 or
        lock.get('adapter_abi_version')!=1 or lock.get('sdk_abi_version')!=65536 or
        not lock.get('adapter_id') or not lock.get('version')):
        raise AdapterError('Invalid pinned GameAdapter lock')
    prefix='https://github.com/zhaoqxu/san14-mods/releases/download/'+lock.get('tag','')+'/'
    if not lock.get('tag') or not lock.get('url','').startswith(prefix) or not re.fullmatch(r'[A-Za-z0-9._-]+\.zip',lock['url'][len(prefix):]):
        raise AdapterError('GameAdapter URL must identify a fixed san14-mods release asset')
    return lock


def reject_links(path,boundary):
    for candidate in (path,*path.parents):
        if candidate.is_symlink() or candidate.is_junction():
            raise AdapterError('Links are not allowed in GameAdapter output or cache paths')
        if candidate==boundary:break


def build_path(root,path):
    build=root/'build'
    if not path.is_absolute():path=root/path
    reject_links(path,root)
    path=path.resolve()
    if path==build.resolve() or not path.is_relative_to(build.resolve()):
        raise AdapterError('GameAdapter output and cache must stay under this checkout build/')
    return path


def open_download(url):
    context=ssl.create_default_context()
    if os.name=='nt':
        try:
            import truststore
        except ImportError:pass
        else:context=truststore.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    return urlopen(Request(url,headers={'User-Agent':'san14-mods-adapter-download'}),timeout=60,context=context)


def check_archive(path,lock):
    if path.stat().st_size!=lock['archive_bytes'] or digest(path)!=lock['sha256']:
        raise AdapterError('GameAdapter archive SHA-256 or size mismatch; no files were extracted')


def archive_files(archive):
    result={};folded=set();total=0
    reserved=re.compile(r'^(?:CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$)',re.I)
    for entry in archive.infolist():
        name=entry.filename
        if entry.is_dir():continue
        path=PurePosixPath(name)
        if (entry.orig_filename!=name or not name or '\\' in name or ':' in name or path.is_absolute() or
            any(part in {'.','..',''} or part.endswith((' ','.')) or reserved.match(part) for part in name.split('/')) or
            stat.S_ISLNK(entry.external_attr>>16) or name.casefold() in folded):
            raise AdapterError('Unsafe or duplicate path in GameAdapter archive')
        allowed=name in {'README.txt','THIRD-PARTY-NOTICES.txt','SAN14Base/GameAdapter/GameAdapter.dll','SAN14Base/GameAdapter/adapter.json'} or name.startswith('licenses/')
        if not allowed:raise AdapterError('Unexpected GameAdapter archive payload')
        total+=entry.file_size
        if total>512*1024*1024:raise AdapterError('GameAdapter extraction size limit exceeded')
        folded.add(name.casefold());result[name]=entry
    required={'README.txt','THIRD-PARTY-NOTICES.txt','SAN14Base/GameAdapter/GameAdapter.dll','SAN14Base/GameAdapter/adapter.json'}
    if not required.issubset(result) or not any(name.startswith('licenses/') for name in result):
        raise AdapterError('GameAdapter binary, manifest or distribution notices are missing')
    return result


def ensure_adapter(root,output,*,offline=False,archive_path=None):
    root=Path(root).resolve();lock=read_lock(root)
    output=build_path(root,Path(output))
    cache=build_path(root,Path('build/.cache/gameadapter')/(lock['sha256']+'.zip'))
    downloaded=False
    if archive_path is not None:
        archive_path=Path(archive_path).resolve();check_archive(archive_path,lock)
    if cache.exists():check_archive(cache,lock)
    else:
        if archive_path is None and offline:
            raise AdapterError('GameAdapter cache is missing in offline mode; run online once or provide --archive')
        cache.parent.mkdir(parents=True,exist_ok=True)
        handle,name=tempfile.mkstemp(prefix='.download-',suffix='.tmp',dir=cache.parent);os.close(handle)
        temporary=Path(name)
        try:
            if archive_path is not None:shutil.copyfile(archive_path,temporary)
            else:
                with open_download(lock['url']) as response,temporary.open('wb') as stream:
                    size=0
                    while block:=response.read(1024*1024):
                        size+=len(block)
                        if size>lock['archive_bytes']:raise AdapterError('GameAdapter download exceeds pinned size')
                        stream.write(block)
                downloaded=True
            check_archive(temporary,lock)
            os.replace(temporary,cache)
        finally:temporary.unlink(missing_ok=True)
    with ZipFile(cache) as archive:
        files=archive_files(archive)
        base='SAN14Base/GameAdapter/'
        manifest=json.loads(archive.read(base+'adapter.json').decode('utf-8-sig'))
        if (manifest.get('schema_version')!=1 or manifest.get('abi_version')!=lock['adapter_abi_version'] or
            manifest.get('sdk_abi_version')!=lock['sdk_abi_version'] or manifest.get('adapter_id')!=lock['adapter_id'] or
            manifest.get('adapter_version')!=lock['version'] or manifest.get('binary')!='GameAdapter.dll' or
            manifest.get('sha256')!=lock['adapter_sha256'] or manifest.get('embedded_commands') is not True or
            sha256(archive.read(base+'GameAdapter.dll')).hexdigest()!=lock['adapter_sha256']):
            raise AdapterError('GameAdapter binary or ABI does not match the pinned manifest')
        if output.exists():
            for path in output.rglob('*'):
                reject_links(path,root)
                if path.is_file() and path.relative_to(output).as_posix() not in files:
                    raise AdapterError('Refusing to overwrite an adapter directory with unexpected files')
        output.parent.mkdir(parents=True,exist_ok=True)
        replaced=0
        with tempfile.TemporaryDirectory(prefix='.adapter-stage-',dir=output.parent) as stage:
            for name in files:
                target=output/name;reject_links(target,root)
                data=archive.read(name)
                if target.is_file() and digest(target)==sha256(data).hexdigest():continue
                staged=Path(stage)/name;staged.parent.mkdir(parents=True,exist_ok=True);staged.write_bytes(data)
                target.parent.mkdir(parents=True,exist_ok=True);os.replace(staged,target);replaced+=1
    return {'component':'GameAdapter','version':lock['version'],'tag':lock['tag'],'archive_sha256':lock['sha256'],
            'downloaded':downloaded,'files':len(files),'replaced_files':replaced,'output':str(output)}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--offline',action='store_true')
    parser.add_argument('--archive',type=Path)
    args=parser.parse_args()
    try:print(json.dumps(ensure_adapter(args.root,args.output,offline=args.offline,archive_path=args.archive)))
    except (OSError,ValueError,BadZipFile) as error:parser.exit(1,str(error)+'\n')
