#!/usr/bin/env python3
"""Package already-built binaries; never claims to compile or test them."""
from pathlib import Path
import hashlib
import subprocess
import zipfile
root=Path(__file__).resolve().parents[1]
dist=root/'dist'
dist.mkdir(exist_ok=True)
with zipfile.ZipFile(dist/'AudioBridge-Windows-x64.zip','w',zipfile.ZIP_DEFLATED) as z:
    for p in (dist/'windows').glob('*.exe'): z.write(p,p.name)
    for name in ('README.md','VALIDATION.md'): z.write(root/name,name)
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
