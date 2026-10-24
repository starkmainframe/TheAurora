#!/usr/bin/env python3
"""Run the actual firmware's portable parser/renderer under native sanitizers."""
import importlib.util
import os
from pathlib import Path
import re
import shutil
import subprocess
import zlib
import struct

root = Path(__file__).resolve().parents[1]
os.chdir(root)

def strip_comments(src):
    out = []
    i = 0
    while i < len(src):
        if src.startswith('//', i):
            end = src.find('\n', i)
            i = len(src) if end < 0 else end
            continue
        if src.startswith('/*', i):
            end = src.find('*/', i + 2)
            if end < 0:
                raise AssertionError('unclosed block comment')
            i = end + 2
            continue
        quote = src[i]
        if quote in '"\'':
            out.append(quote)
            i += 1
            while i < len(src):
                if src[i] == '\\':
                    out.append(src[i:i + 2])
                    i += 2
                    continue
                out.append(src[i])
                if src[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        out.append(src[i])
        i += 1
    return ''.join(out)

def function_body(src, name):
    match = None
    for found in re.finditer(rf'\b{name}\s*\([^;{{}}]*\)\s*\{{', src):
        match = found
        break
    if not match:
        raise AssertionError(f'missing function {name}')
    depth = 1
    i = match.end()
    while i < len(src) and depth:
        if src[i] == '{':
            depth += 1
        elif src[i] == '}':
            depth -= 1
        i += 1
    if depth:
        raise AssertionError(f'unbalanced function {name}')
    return src[match.end():i - 1]

def enclosing_function(src, index):
    depth = 0
    for i in range(index, -1, -1):
        if src[i] == '}':
            depth += 1
        elif src[i] == '{':
            if depth == 0:
                found = re.search(r'\b([A-Za-z_][A-Za-z0-9_]*)\s*\([^;{}]*\)\s*$', src[:i])
                if not found:
                    raise AssertionError('WiFi.useStaticBuffers(true) is not inside a function')
                return found.group(1)
            depth -= 1
    raise AssertionError('WiFi.useStaticBuffers(true) is outside a function')

def check_wifi_boot(network, main):
    net = strip_comments(network)
    call = re.search(r'WiFi\s*\.\s*useStaticBuffers\s*\(\s*true\s*\)', net)
    mode = re.search(r'WiFi\s*\.\s*mode\s*\(', net)
    if not call:
        raise AssertionError('WiFi.useStaticBuffers(true) is missing')
    if not mode or call.start() > mode.start():
        raise AssertionError('WiFi.useStaticBuffers(true) must appear before the first WiFi.mode')
    helper = enclosing_function(net, call.start())
    helper_body = function_body(net, helper)
    if re.search(r'WiFi\s*\.\s*mode\s*\(', helper_body):
        raise AssertionError(f'{helper} calls WiFi.mode')
    begin = function_body(net, 'networkBegin')
    invoked = re.search(rf'\b{helper}\s*\(', begin)
    begin_mode = re.search(r'WiFi\s*\.\s*mode\s*\(', begin)
    if not invoked or not begin_mode or invoked.start() > begin_mode.start():
        raise AssertionError('networkBegin must call the static-buffer helper before WiFi.mode')
    if re.search(r'\breturn\b', begin[:invoked.start()]):
        raise AssertionError('networkBegin can return before the static-buffer call')
    empty = begin.find('ssid.isEmpty()')
    if empty < 0 or begin_mode.start() > empty:
        raise AssertionError('empty NVS still reaches WiFi.mode before the setup-AP branch')
    setup = function_body(strip_comments(main), 'setup')
    order = [setup.find(name) for name in ('networkBegin', 'uiBegin', 'dataBegin')]
    if any(index < 0 for index in order) or order != sorted(order):
        raise AssertionError('setup() must call networkBegin, then uiBegin, then dataBegin')

def parse_partitions(text):
    rows = []
    for raw in text.splitlines():
        line = raw.split('#', 1)[0].strip()
        if not line:
            continue
        fields = [part.strip() for part in line.split(',')]
        if not fields or fields[0].lower() == 'name':
            continue
        if len(fields) < 5:
            raise AssertionError(f'short partition row: {line}')
        rows.append((fields[0], fields[1], fields[2], int(fields[3], 0), int(fields[4], 0)))
    return rows

def check_partitions(rows, data_cpp):
    flash = 16 * 1024 * 1024
    spans = []
    for name, kind, subtype, offset, size in rows:
        if size <= 0 or offset % 0x1000 or size % 0x1000:
            raise AssertionError(f'{name} is not a positive 4 KB-aligned partition')
        end = offset + size
        if end > flash:
            raise AssertionError(f'{name} ends past 16 MB')
        for start, stop, other in spans:
            if end > start and offset < stop:
                raise AssertionError(f'{name} overlaps {other}')
        spans.append((offset, end, name))
    little = [row for row in rows if row[0] == 'littlefs']
    if len(little) != 1 or little[0][1] != 'data' or little[0][2] != 'spiffs':
        raise AssertionError('littlefs must stay one data/spiffs partition')
    label = re.search(r'LittleFS\.begin\(\s*true\s*,\s*"/littlefs"\s*,\s*10\s*,\s*"([^"]+)"\s*\)', data_cpp)
    if not label or label.group(1) != 'littlefs':
        raise AssertionError('LittleFS mount label must be littlefs')
    dump = [row for row in rows if row[2] == 'coredump']
    if len(dump) != 1 or dump[0][:3] != ('coredump', 'data', 'coredump'):
        raise AssertionError('coredump partition is missing')
    if dump[0][3] != 0xFF0000 or dump[0][4] != 0x10000 or little[0][3] + little[0][4] != dump[0][3]:
        raise AssertionError('coredump must be 64 KB at 0xFF0000, directly after littlefs')

def load_packager():
    spec = importlib.util.spec_from_file_location('package_firmware', root / 'tools/package_firmware.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def independent_flash_mode(ini_text, envs=('esp32-s3-amoled-143', 'esp32-s3-amoled-175')):
    """Raw board_build.flash_mode. This must not apply pio's qio→dio downgrade."""
    sections = {}
    current = None
    for raw in ini_text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1].strip()
            sections.setdefault(current, {})
            continue
        if current and '=' in line:
            key, value = line.split('=', 1)
            sections[current][key.strip()] = value.strip().strip('"').strip("'")
    def lookup(section, key):
        seen = set()
        queue = [section]
        while queue:
            name = queue.pop(0)
            if name in seen:
                continue
            seen.add(name)
            body = sections.get(name)
            if body is None:
                raise AssertionError(f'missing [{name}]')
            if key in body:
                return body[key]
            queue.extend(part.strip() for part in body.get('extends', '').split(',') if part.strip())
        return None
    modes = [lookup(f'env:{env}', 'board_build.flash_mode') for env in envs]
    if any(mode is None for mode in modes) or len(set(modes)) != 1:
        raise AssertionError(f'board flash modes differ or are missing: {modes}')
    return modes[0]

def check_flash_mode(packager, ini_text):
    mode, _freq, _size = packager.flash_settings(ini_text)
    raw = independent_flash_mode(ini_text)
    if mode != raw:
        raise AssertionError(f'merge flash_mode {mode} != platformio.ini board_build.flash_mode {raw}')
    if 'qio_opi' in ini_text and raw != 'qio':
        raise AssertionError('memory_type qio_opi requires board_build.flash_mode qio')
    tweaked = ini_text.replace('board_build.flash_mode = qio', 'board_build.flash_mode = dout', 1)
    if 'board_build.flash_mode = dout' not in tweaked:
        raise AssertionError('flash-mode derivation fixture did not apply')
    if packager.flash_settings(tweaked)[0] != 'dout':
        raise AssertionError('merge flash_mode is hardcoded and ignores platformio.ini')
    disagree = ini_text.replace(
        '[env:esp32-s3-amoled-175]\nextends = amoled_common\n',
        '[env:esp32-s3-amoled-175]\nextends = amoled_common\nboard_build.flash_mode = dio\n',
        1)
    try:
        packager.flash_settings(disagree)
    except SystemExit:
        return
    raise AssertionError('different board flash modes were accepted')

def expect_failure(func, *args):
    try:
        func(*args)
    except AssertionError:
        return
    raise AssertionError(f'{getattr(func, "__name__", func)} accepted an invalid fixture')

def check_firmware_contract():
    network = (root / 'src/network.cpp').read_text()
    main = (root / 'src/main.cpp').read_text()
    data = (root / 'src/data.cpp').read_text()
    ini = (root / 'platformio.ini').read_text()
    check_wifi_boot(network, main)
    check_partitions(parse_partitions((root / 'partitions.csv').read_text()), data)
    packager = load_packager()
    check_flash_mode(packager, ini)
    good_net = '''
    void keepStaticWifiBuffers() { WiFi.useStaticBuffers(true); }
    void startAP() { WiFi.mode(WIFI_AP_STA); }
    void networkBegin() { keepStaticWifiBuffers(); WiFi.mode(WIFI_STA); if (ssid.isEmpty()) startAP(); }
    '''
    good_main = 'void setup() { networkBegin(); uiBegin(); dataBegin(); }'
    check_wifi_boot(good_net, good_main)
    expect_failure(check_wifi_boot, good_net.replace(
        'void keepStaticWifiBuffers() { WiFi.useStaticBuffers(true); }\n    void startAP() { WiFi.mode(WIFI_AP_STA); }',
        'void startAP() { WiFi.mode(WIFI_AP_STA); }\n    void keepStaticWifiBuffers() { WiFi.useStaticBuffers(true); }'), good_main)
    expect_failure(check_wifi_boot, good_net.replace('keepStaticWifiBuffers(); WiFi.mode', 'WiFi.mode'), good_main)
    expect_failure(check_wifi_boot, good_net, 'void setup() { uiBegin(); networkBegin(); dataBegin(); }')
    expect_failure(check_partitions, parse_partitions(
        'littlefs, data, spiffs, 0xc10000, 0x3f0000,\ncoredump, data, coredump, 0xFF0000, 0x10000,\n'), data)
    expect_failure(check_partitions, parse_partitions(
        'littlefs, data, spiffs, 0xc10000, 0x3e0000,\ncoredump, data, coredump, 0xFF0000, 0x20000,\n'), data)
    print('Empty NVS: setup -> networkBegin -> WiFi.useStaticBuffers(true) -> WiFi.mode(WIFI_STA) -> startAP.')
    print('uiBegin and LittleFS run only after that. The setup AP log is not reached when WiFi init panics.')
    qemu = [name for name in ('qemu-system-xtensa', 'qemu-system-xtensaeb') if shutil.which(name)]
    if qemu:
        print(f'QEMU present ({", ".join(qemu)}); no esp32s3 machine boot was attempted from this check.')
    else:
        print('QEMU esp32s3: not available. Only a device can prove the Setup AP line.')
    for env in ('esp32-s3-amoled-143', 'esp32-s3-amoled-175'):
        path = root / '.pio/build' / env / 'firmware.map'
        if not path.exists():
            print(f'DRAM {env}: map not present')
            continue
        text = path.read_text(errors='replace')
        segment = re.search(r'^dram0_0_seg\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)', text, re.M)
        heap = re.search(r'(0x[0-9a-fA-F]+)\s+_heap_start\s*=', text)
        if not segment or not heap:
            raise AssertionError(f'{env} map has no DRAM heap bounds')
        origin, length = int(segment.group(1), 16), int(segment.group(2), 16)
        start = int(heap.group(1), 16)
        end = origin + length
        print(f'DRAM {env}: free internal {end - start} bytes (_heap_start {start:#x} .. {end:#x})')
    print('Firmware contract: static WiFi buffers, flash mode and partition table.')

check_firmware_contract()
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
