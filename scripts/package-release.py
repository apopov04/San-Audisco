#!/usr/bin/env python3
"""Package already-built binaries; never claims to compile or test them."""
from pathlib import Path
import hashlib
import json
from datetime import datetime, timezone
import subprocess
import zipfile
root=Path(__file__).resolve().parents[1]
dist=root/'dist'
dist.mkdir(exist_ok=True)
artifacts = {
    'macOS-arm64-gui': dist/'AudioBridge.app/Contents/MacOS/AudioBridge',
    'macOS-arm64-engine': dist/'AudioBridge.app/Contents/MacOS/bridge-engine',
    'Windows-x64-gui': dist/'windows/AudioBridgeApp.exe',
    'Windows-x64-engine': dist/'windows/audiobridge.exe',
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
    'version': '0.1.0', 'source_commit': commit, 'source_dirty': dirty,
    'packaged_at_utc': datetime.now(timezone.utc).isoformat(),
    'binary_sha256': {name: hashlib.sha256(path.read_bytes()).hexdigest() for name, path in artifacts.items()},
    'validation': 'See VALIDATION.md for native Windows/AirPods smoke results and LAN findings; full two-device audio and Deadlock acceptance is pending.',
}
(dist/'BUILD_INFO.json').write_text(json.dumps(manifest, indent=2)+'\n')
with zipfile.ZipFile(dist/'AudioBridge-Windows-x64.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in (dist/'windows').glob('*.exe'): z.write(p,p.name)
    z.write(dist/'BUILD_INFO.json','BUILD_INFO.json')
    for name in ('README.md','VALIDATION.md','HARDWARE_TEST.md'): z.write(root/name,name)
    for p in (root/'third_party/licenses').glob('*'): z.write(p,'licenses/'+p.name)
with zipfile.ZipFile(dist/'AudioBridge-source.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in root.rglob('*'):
        if not p.is_file():continue
        rel=p.relative_to(root)
        if any(x in ('.git','dist','build','deps','__pycache__') for x in rel.parts):continue
        if p.suffix in ('.key','.conf') or p.name=='.DS_Store':continue
        z.write(p,'AudioBridge/'+str(rel))
app=dist/'AudioBridge.app'
if app.exists():
    subprocess.run(['ditto','-c','-k','--sequesterRsrc','--keepParent',str(app),str(dist/'AudioBridge-macOS-arm64.zip')],check=True)
archives=sorted(dist.glob('*.zip'))
(dist/'SHA256SUMS.txt').write_text(''.join(hashlib.sha256(p.read_bytes()).hexdigest()+'  '+p.name+'\n' for p in archives))
for p in archives: print(p)
