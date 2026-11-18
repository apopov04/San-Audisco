#!/usr/bin/env python3
"""Convert the approved artwork into native icon containers without redrawing it."""
from pathlib import Path
import struct
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
assets = root/'assets'
source = assets/'san-audisco-icon-approved.png'
with tempfile.TemporaryDirectory(prefix='san-audisco-icons-') as tmp:
    folder = Path(tmp)
    iconset = folder/'SanAudisco.iconset'
    iconset.mkdir()
    for size in (16, 32, 128, 256, 512):
        for scale in (1, 2):
            pixels = size*scale
            name = f'icon_{size}x{size}'+('@2x' if scale == 2 else '')+'.png'
            subprocess.run(['sips', '-z', str(pixels), str(pixels), str(source), '--out', str(iconset/name)], check=True, stdout=subprocess.DEVNULL)
    subprocess.run(['iconutil', '-c', 'icns', str(iconset), '-o', str(assets/'SanAudisco.icns')], check=True)
    images = []
    for size in (16, 24, 32, 48, 64, 128, 256):
        file = folder/f'windows-{size}.bmp'
        subprocess.run(['sips', '-z', str(size), str(size), '-s', 'format', 'bmp', str(source), '--out', str(file)], check=True, stdout=subprocess.DEVNULL)
        bmp = file.read_bytes()
        start = struct.unpack_from('<I', bmp, 10)[0]
        header_size, width, height, planes, bits, compression = struct.unpack_from('<IiiHHI', bmp, 14)
        if (header_size, width, abs(height), planes, bits, compression) != (40, size, size, 1, 24, 0):
            raise ValueError('Unexpected BMP conversion format')
        stride = (size*3+3)//4*4
        rows = [bmp[start+y*stride:start+y*stride+size*3] for y in range(size)]
        if height < 0:
            rows.reverse()
        pixels = b''.join(row[x:x+3]+b'\xff' for row in rows for x in range(0, size*3, 3))
        mask = bytes(((size+31)//32)*4*size)
        dib = struct.pack('<IiiHHIIiiII', 40, size, size*2, 1, 32, 0, len(pixels)+len(mask), 0, 0, 0, 0)
        images.append((size, dib+pixels+mask))
    offset = 6+16*len(images)
    header = struct.pack('<HHH', 0, 1, len(images))
    entries, payload = b'', b''
    for size, data in images:
        entries += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
        payload += data
        offset += len(data)
    (assets/'SanAudisco.ico').write_bytes(header+entries+payload)
print('Created assets/SanAudisco.icns and assets/SanAudisco.ico')
