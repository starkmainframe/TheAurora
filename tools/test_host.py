#!/usr/bin/env python3
"""Run the actual firmware's portable parser/renderer under native sanitizers."""
import os
from pathlib import Path
import subprocess
import zlib
import struct

root = Path(__file__).resolve().parents[1]
os.chdir(root)
(root / 'build').mkdir(exist_ok=True)
headers = root / '.pio/libdeps/esp32-s3-amoled-143/ArduinoJson/src'
if not headers.exists():
    raise SystemExit('Run pio pkg install -e esp32-s3-amoled-143 first.')
compiler = os.environ.get('CXX', 'g++')
subprocess.run([compiler, '-std=c++17', '-O0', '-g', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-Iinclude', '-isystem', str(headers),
                'src/model.cpp', 'src/render.cpp', 'tests/host.cpp', '-o', 'build/host-tests'], check=True)
subprocess.run(['build/host-tests'], check=True)
# Dependency-free PPM -> PNG: the artifact uses the identical RGB565 pixels.
ppm = (root / 'build/globe.ppm').read_bytes()
header, pixels = ppm.split(b'255\n', 1)
assert header == b'P6\n466 466\n' and len(pixels) == 466*466*3
scanlines = b''.join(b'\0'+pixels[y*1398:(y+1)*1398] for y in range(466))
def chunk(kind, data):
    return struct.pack('>I', len(data))+kind+data+struct.pack('>I', zlib.crc32(kind+data)&0xffffffff)
png = b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',466,466,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(scanlines,9))+chunk(b'IEND',b'')
(root/'build/globe.png').write_bytes(png)
print('Headless globe written to build/globe.png')
