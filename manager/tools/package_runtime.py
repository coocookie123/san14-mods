"""Compose the manager core with an independently supplied binary adapter package."""
import argparse, hashlib, json, zipfile
from pathlib import Path
from adapter_package import validate_adapter

def package(core,adapter,output,runtime=None):
    validate_adapter(adapter)
    version=json.loads((Path(__file__).resolve().parents[1]/'project.json').read_text('utf-8'))['version']
    metadata=json.dumps({'schema_version':1,'version':version,'adapter_abi_version':1,'embedded_commands':True},indent=2).encode('utf-8')
    files={'dinput8.dll':core,'SAN14Base/GameAdapter/adapter.json':adapter/'SAN14Base/GameAdapter/adapter.json','SAN14Base/GameAdapter/GameAdapter.dll':adapter/'SAN14Base/GameAdapter/GameAdapter.dll'}
    if runtime:
        for p in sorted(runtime.rglob('*')):
            if p.is_symlink():raise ValueError('Runtime symlinks refused')
            if p.is_file():
                rel=p.relative_to(runtime).as_posix()
                if p.suffix.lower() in {'.pdb','.obj','.lib','.exp','.cpp','.h','.py','.js'}:raise ValueError('Source/debug file in runtime: '+rel)
                files['SAN14Base/worker/'+rel]=p
    output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED) as z:
        z.writestr('package.json',metadata)
        for rel,p in sorted(files.items()):
            if p.is_symlink() or not p.is_file():raise ValueError('Missing/unsafe package input: '+rel)
            z.writestr(rel,p.read_bytes())
    with zipfile.ZipFile(output) as z:
        if z.testzip() is not None:raise ValueError('Package CRC failed')
        if z.read('package.json')!=metadata:raise ValueError('Package metadata changed')
        for rel,p in files.items():
            if z.read(rel)!=p.read_bytes():raise ValueError('Package bytes changed')
    return {'files':len(files)+1,'version':version,'sha256':hashlib.sha256(output.read_bytes()).hexdigest()}

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--core',required=True,type=Path);p.add_argument('--adapter',required=True,type=Path);p.add_argument('--output',required=True,type=Path);p.add_argument('--runtime',type=Path);a=p.parse_args()
    print(json.dumps(package(a.core,a.adapter,a.output,a.runtime)))
