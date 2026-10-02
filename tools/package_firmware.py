#!/usr/bin/env python3
"""Merge both ESP32-S3 images at their actual flash offsets for ESP Web Tools."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument('--core-dir', default=os.environ.get('PLATFORMIO_CORE_DIR', str(Path.home()/'.platformio')))
args = parser.parse_args()
core = Path(args.core_dir)
esptool = core/'packages/tool-esptoolpy/esptool.py'
boot_app = core/'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
metadata = {'version':'1.0.0', 'commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(), 'boards':{}}
for board in ('143','175'):
    build = root/'.pio/build'/f'esp32-s3-amoled-{board}'
    target = root/'web/flash'/f'firmware-{board}.bin'
    parts = [(0,build/'bootloader.bin'),(0x8000,build/'partitions.bin'),(0xe000,boot_app),(0x10000,build/'firmware.bin')]
    for index,(offset,path) in enumerate(parts):
        if not path.is_file(): raise SystemExit(f'Missing {path}; build both PlatformIO environments first.')
        if index+1 < len(parts) and offset+path.stat().st_size > parts[index+1][0]:
            raise SystemExit(f'Overlapping flash part: {path}')
    python = core/'penv/bin/python'
    command = [str(python) if python.exists() else sys.executable,str(esptool),'--chip','esp32s3','merge_bin','-o',str(target),
               '--flash_mode','dio','--flash_freq','80m','--flash_size','16MB']
    for offset,path in parts: command.extend([hex(offset),str(path)])
    subprocess.run(command,check=True)
    image = target.read_bytes()
    # esptool updates the boot header's flash parameters; all other regions must match.
    for offset,path in parts[1:]:
        raw=path.read_bytes(); assert image[offset:offset+len(raw)] == raw
    assert image[0] == 0xe9 and image[0x10000] == 0xe9
    metadata['boards'][board]={'file':target.name,'bytes':len(image),'sha256':hashlib.sha256(image).hexdigest()}
(root/'web/flash/build.json').write_text(json.dumps(metadata,indent=2)+'\n')
preview=root/'build/globe.png'
if not preview.exists(): raise SystemExit('Run python3 tools/test_host.py before packaging to generate the globe preview.')
shutil.copyfile(preview,root/'web/flash/globe.png')
print('Packaged and verified both board images, manifests and renderer preview.')
