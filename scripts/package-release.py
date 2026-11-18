#!/usr/bin/env python3
"""Package already-built binaries; never claims to compile or test them."""
from pathlib import Path
import hashlib
import json
from datetime import datetime, timezone
import subprocess
import zipfile
import argparse
root=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--dist',type=Path,default=root/'dist')
dist=parser.parse_args().dist.resolve()
dist.mkdir(exist_ok=True)
artifacts = {
    'macOS-arm64-gui': dist/'San Audisco.app/Contents/MacOS/SanAudisco',
    'macOS-arm64-engine': dist/'San Audisco.app/Contents/MacOS/bridge-engine',
    'macOS-arm64-agent': dist/'San Audisco.app/Contents/MacOS/bridge-agent',
    'Windows-x64-gui': dist/'windows/SanAudisco.exe',
    'Windows-x64-engine': dist/'windows/san-audisco.exe',
    'Windows-x64-agent': dist/'windows/bridge-agent.exe',
}
for name, path in artifacts.items():
    if not path.is_file():
        raise SystemExit(f'Missing required build: {name}: {path}')
try:
    commit = subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True, stderr=subprocess.DEVNULL).strip()
    dirty = bool(subprocess.check_output(['git', '-C', str(root), 'status', '--porcelain'], text=True))
except (subprocess.CalledProcessError, FileNotFoundError):
    commit, dirty = None, None
manifest = {
    'version': '0.3.3', 'source_commit': commit, 'source_dirty': dirty,
    'packaged_at_utc': datetime.now(timezone.utc).isoformat(),
    'binary_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in artifacts.items()},
    'validation': 'See docs/V033_QUIT.md for repeat-Quit and unavailable-output checks. Restoration warnings no longer veto shutdown. Pairing and recovery records remain intact.',
}
(dist/'BUILD_INFO.json').write_text(json.dumps(manifest, indent=2)+'\n')
with zipfile.ZipFile(dist/'San Audisco-Windows-x64.zip','w',zipfile.ZIP_DEFLATED) as z:
    for name in ('SanAudisco.exe','san-audisco.exe','bridge-agent.exe'): z.write(dist/'windows'/name,name)
    z.write(dist/'BUILD_INFO.json','BUILD_INFO.json')
    for name in ('README.md','VALIDATION.md','HARDWARE_TEST.md','PRIVATE_SETUP.md'): z.write(root/name,name)
    for p in (root/'third_party/licenses').glob('*'): z.write(p,'licenses/'+p.name)
    z.write(root/'third_party/json/LICENSE.MIT','licenses/nlohmann-json.txt')
    for p in (root/'docs').glob('*.md'): z.write(p,'docs/'+p.name)
with zipfile.ZipFile(dist/'San Audisco-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    # Only version-controlled source is eligible; private vendor packages are not.
    files=subprocess.check_output(['git','-C',str(root),'ls-files','-z']).decode().split('\0')
    for name in files:
        if not name:continue
        p=root/name
        if not p.is_file() or p.is_symlink():continue
        rel=p.relative_to(root)
        if any(x in ('.git','dist','build','deps','__pycache__') for x in rel.parts):continue
        if p.suffix.lower() in ('.key','.conf','.zip','.pkg','.dmg','.exe','.dll') or p.name=='.DS_Store':continue
        z.write(p,'San Audisco/'+str(rel))
app=dist/'San Audisco.app'
if app.exists():
    subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(app),str(dist/'San Audisco-macOS-arm64.zip')],check=True)
archives=sorted(dist.glob('*.zip'))
(dist/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in archives))
for p in archives: print(p)
