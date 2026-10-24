#!/usr/bin/env python3
"""Run the actual firmware's portable parser/renderer under native sanitizers."""
import ast
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

def check_wifi_boot(network, main, ini):
    net = strip_comments(network)
    if re.search(r'\buseStaticBuffers\s*\(', net):
        raise AssertionError('useStaticBuffers does not create static TX buffers or clear sta_disconnected_pm')
    wrap = function_body(net, '__wrap_esp_wifi_init')
    copy = re.search(r'=\s*\*\s*config\s*;', wrap)
    assign = re.search(r'sta_disconnected_pm\s*=\s*false\s*;', wrap)
    real = re.search(r'\b__real_esp_wifi_init\s*\(', wrap)
    if not copy or not assign or not real or not (copy.start() < assign.start() < real.start()):
        raise AssertionError('__wrap_esp_wifi_init must copy the config and clear sta_disconnected_pm before __real_esp_wifi_init')
    if 'offsetof(wifi_init_config_t, sta_disconnected_pm) == 200' not in net:
        raise AssertionError('sta_disconnected_pm must stay at byte 200, the offset wifi_menuconfig_init reads')
    if '--wrap=esp_wifi_init' not in ini:
        raise AssertionError('esp_wifi_init must be wrapped so the library cannot init with disconnected PM on')
    begin = function_body(net, 'networkBegin')
    if 'ssid.isEmpty()' not in begin or 'startAP' not in begin:
        raise AssertionError('empty NVS must still reach startAP')
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
    """Return (raw mode, memory type, pio upload mode).

    Upload mode copies builder/main.py _get_board_flash_mode and does not
    call package_firmware.upload_flash_mode. qio/qout become dio. opi_opi and
    opi_qspi become dout. The raw mode stays the library selection.
    """
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
    raw_modes = []
    memories = []
    for env in envs:
        raw_modes.append(lookup(f'env:{env}', 'board_build.flash_mode'))
        memories.append(lookup(f'env:{env}', 'board_build.arduino.memory_type') or '')
    if any(mode is None for mode in raw_modes) or len(set(raw_modes)) != 1 or len(set(memories)) != 1:
        raise AssertionError(f'board flash modes differ or are missing: {raw_modes} {memories}')
    raw, memory = raw_modes[0], memories[0]
    if memory in ('opi_opi', 'opi_qspi'):
        upload = 'dout'
    elif raw in ('qio', 'qout'):
        upload = 'dio'
    else:
        upload = raw
    return raw, memory, upload

def check_flash_mode(packager, ini_text):
    mode, freq, size = packager.flash_settings(ini_text)
    raw, memory, upload = independent_flash_mode(ini_text)
    if mode != upload:
        raise AssertionError(f'merge flash_mode {mode} != pio upload mode {upload} (ini {raw}, memory {memory})')
    if memory == 'qio_opi':
        if raw != 'qio':
            raise AssertionError('memory_type qio_opi requires board_build.flash_mode qio for the library')
        if mode != 'dio' or packager.header_bytes(mode, freq, size)[0] != 2:
            raise AssertionError('memory_type qio_opi requires merged boot header byte 2 = DIO')
    tweaked = ini_text.replace('board_build.flash_mode = qio', 'board_build.flash_mode = dout', 1)
    if 'board_build.flash_mode = dout' not in tweaked:
        raise AssertionError('flash-mode derivation fixture did not apply')
    if packager.flash_settings(tweaked)[0] != 'dout':
        raise AssertionError('merge flash_mode is hardcoded and ignores platformio.ini')
    qout = ini_text.replace('board_build.flash_mode = qio', 'board_build.flash_mode = qout', 1)
    qout_mode, qout_freq, qout_size = packager.flash_settings(qout)
    if qout_mode != 'dio' or packager.header_bytes(qout_mode, qout_freq, qout_size)[0] != 2:
        raise AssertionError('qout must merge as dio, boot header byte 2')
    opi = ini_text.replace('board_build.arduino.memory_type = qio_opi', 'board_build.arduino.memory_type = opi_qspi', 1)
    if 'opi_qspi' not in opi or packager.flash_settings(opi)[0] != 'dout':
        raise AssertionError('opi_qspi must merge as dout')
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

FLOAT_CONVERSION = re.compile(
    r"%(?:\d+\$)?[-+ #0']*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|h|ll|l|j|z|t|L)?[eEfFgGaA]")

def call_arguments(src, open_index):
    """Split a call's top-level comma arguments, ignoring strings and nesting."""
    if src[open_index] != '(':
        raise AssertionError('call parser needs an opening parenthesis')
    args, start, depth, i = [], open_index + 1, 0, open_index + 1
    while i < len(src):
        char = src[i]
        if char in '"\'':
            quote = char
            i += 1
            while i < len(src):
                if src[i] == '\\':
                    i += 2
                    continue
                if src[i] == quote:
                    break
                i += 1
            i += 1
            continue
        if char in '([{':
            depth += 1
        elif char in ')]}':
            if depth == 0:
                if char != ')':
                    raise AssertionError('unbalanced call')
                args.append(src[start:i])
                return args, i
            depth -= 1
        elif char == ',' and depth == 0:
            args.append(src[start:i])
            start = i + 1
        i += 1
    raise AssertionError('unbalanced call')

def string_literal_value(expr):
    """Return the value of a (possibly concatenated) string literal, else None."""
    expr = expr.strip()
    values = []
    i = 0
    while i < len(expr):
        while i < len(expr) and expr[i].isspace():
            i += 1
        if i == len(expr):
            break
        if expr[i] != '"':
            return None
        j = i + 1
        while j < len(expr):
            if expr[j] == '\\':
                j += 2
                continue
            if expr[j] == '"':
                break
            j += 1
        if j >= len(expr):
            return None
        try:
            values.append(ast.literal_eval(expr[i:j + 1]))
        except (SyntaxError, ValueError):
            return None
        i = j + 1
    return ''.join(values) if values else None

def float_conversions(fmt):
    """Conversions in a printf format that consume a floating-point argument."""
    found = []
    i = 0
    while i < len(fmt):
        if fmt[i] != '%':
            i += 1
            continue
        if i + 1 < len(fmt) and fmt[i + 1] == '%':
            i += 2
            continue
        match = FLOAT_CONVERSION.match(fmt, i)
        if match:
            found.append(match.group(0))
            i = match.end()
        else:
            i += 1
    return found

def check_lv_float_formats(sources):
    """Reject LVGL format calls with %f/%e/%g: lv_printf has no float support."""
    problems = []
    for name, text in sources.items():
        clean = strip_comments(text)
        for match in re.finditer(r'\b(lv_[A-Za-z0-9_]*_fmt|lv_snprintf)\s*\(', clean):
            args, _ = call_arguments(clean, match.end() - 1)
            format_index = 2 if match.group(1) == 'lv_snprintf' else 1
            if len(args) <= format_index:
                continue
            fmt = string_literal_value(args[format_index])
            if fmt is None:
                continue
            bad = float_conversions(fmt)
            if bad:
                problems.append(f'{name}: {match.group(1)} uses {", ".join(bad)} in {fmt!r}')
    if problems:
        raise AssertionError(
            'LVGL format call with a float conversion (LV_SPRINTF_USE_FLOAT is off): '
            + '; '.join(problems))

def check_lv_float_format_guard():
    check_lv_float_formats({'good.cpp': '''
        void f() {
          lv_label_set_text_fmt(label, "%s %d", text, count);
          lv_snprintf(buf, sizeof(buf), "%s", text);
          /* lv_label_set_text_fmt(label, "%.2f", bad); */
          lv_label_set_text_fmt(label, "100%%f");
        }
    '''})
    for bad in (
        'lv_label_set_text_fmt(label, "%.4f N", latitude);',
        'lv_snprintf(buf, sizeof(buf), "%e", value);',
        'lv_label_set_text_fmt(label, "%d" "%.2f", count, value);',
        'lv_label_set_text_fmt(label, "%g", value);',
    ):
        expect_failure(check_lv_float_formats, {'bad.cpp': bad})

def check_firmware_contract():
    network = (root / 'src/network.cpp').read_text()
    main = (root / 'src/main.cpp').read_text()
    data = (root / 'src/data.cpp').read_text()
    ini = (root / 'platformio.ini').read_text()
    check_wifi_boot(network, main, ini)
    check_partitions(parse_partitions((root / 'partitions.csv').read_text()), data)
    packager = load_packager()
    check_flash_mode(packager, ini)
    good_net = '''
    static_assert(offsetof(wifi_init_config_t, sta_disconnected_pm) == 200, "byte");
    int __wrap_esp_wifi_init(const wifi_init_config_t *config) {
      wifi_init_config_t cfg = *config;
      cfg.sta_disconnected_pm = false;
      return __real_esp_wifi_init(&cfg);
    }
    void startAP() {}
    void networkBegin() { if (ssid.isEmpty()) startAP(); }
    '''
    good_main = 'void setup() { networkBegin(); uiBegin(); dataBegin(); }'
    good_ini = 'build_flags =\n    -Wl,--wrap=esp_wifi_init\n'
    check_wifi_boot(good_net, good_main, good_ini)
    expect_failure(check_wifi_boot, good_net.replace(
        'cfg.sta_disconnected_pm = false;\n      return __real_esp_wifi_init(&cfg);',
        'return __real_esp_wifi_init(&cfg);\n      cfg.sta_disconnected_pm = false;'), good_main, good_ini)
    expect_failure(check_wifi_boot, good_net.replace(
        'offsetof(wifi_init_config_t, sta_disconnected_pm) == 200',
        'offsetof(wifi_init_config_t, sta_disconnected_pm) == 0'), good_main, good_ini)
    expect_failure(check_wifi_boot, good_net + '\n    void extra() { WiFi.useStaticBuffers(true); }\n', good_main, good_ini)
    expect_failure(check_wifi_boot, good_net, good_main, 'build_flags =\n    -Iinclude\n')
    expect_failure(check_wifi_boot, good_net, 'void setup() { uiBegin(); networkBegin(); dataBegin(); }', good_ini)
    expect_failure(check_partitions, parse_partitions(
        'littlefs, data, spiffs, 0xc10000, 0x3f0000,\ncoredump, data, coredump, 0xFF0000, 0x10000,\n'), data)
    expect_failure(check_partitions, parse_partitions(
        'littlefs, data, spiffs, 0xc10000, 0x3e0000,\ncoredump, data, coredump, 0xFF0000, 0x20000,\n'), data)
    print('Empty NVS: esp_wifi_init runs with sta_disconnected_pm false, then networkBegin may start STA and startAP.')
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
    print('Firmware contract: disconnected PM off, DIO boot header for qio_opi, coredump partition kept.')

check_firmware_contract()
check_lv_float_format_guard()
firmware_sources = {
    str(path.relative_to(root)): path.read_text(errors='replace')
    for path in sorted((root / 'src').rglob('*'))
    if path.suffix in ('.cpp', '.h') and path.is_file()
}
if not firmware_sources:
    raise AssertionError('no src/**/*.cpp|*.h files found for the LVGL format check')
check_lv_float_formats(firmware_sources)
print(f'LVGL formats: no float conversions in {len(firmware_sources)} src files.')
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
