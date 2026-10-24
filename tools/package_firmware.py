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

# ESP32-S3 image header, same encoding as esptool's ESP32S3ROM tables.
# Byte 2 is the flash mode. Byte 3 is the size nibble (high) plus the frequency nibble (low).
FLASH_MODE_BYTE = {'qio': 0, 'qout': 1, 'dio': 2, 'dout': 3}
FLASH_SIZE_HIGH = {
    '1MB': 0x00, '2MB': 0x10, '4MB': 0x20, '8MB': 0x30,
    '16MB': 0x40, '32MB': 0x50, '64MB': 0x60, '128MB': 0x70,
}
FLASH_FREQ_LOW = {'80m': 0x0F, '40m': 0x00, '26m': 0x01, '20m': 0x02}
# The qio_opi board is 80 MHz. platformio.ini does not set board_build.f_flash.
DEFAULT_FLASH_FREQ = '80m'
BOARDS = ('143', '175')
ENVS = tuple(f'esp32-s3-amoled-{board}' for board in BOARDS)

root = Path(__file__).resolve().parents[1]


def ini_sections(text):
    sections = {}
    current = None
    for raw in text.splitlines():
        line = raw.split(';', 1)[0].strip()
        if not line or line.startswith('#'):
            continue
        if line.startswith('[') and line.endswith(']'):
            current = line[1:-1].strip()
            sections.setdefault(current, {})
            continue
        if current is None or '=' not in line:
            continue
        key, value = line.split('=', 1)
        sections[current][key.strip()] = value.strip().strip('"').strip("'")
    return sections


def lookup(sections, section, key):
    seen = set()
    queue = [section]
    while queue:
        name = queue.pop(0)
        if name in seen:
            continue
        seen.add(name)
        body = sections.get(name)
        if body is None:
            raise SystemExit(f'platformio.ini has no [{name}]')
        if key in body:
            return body[key]
        queue.extend(part.strip() for part in body.get('extends', '').split(',') if part.strip())
    return None


def flash_freq_token(value):
    if not value:
        return DEFAULT_FLASH_FREQ
    token = value.strip()
    if len(token) > 1 and token[-1] in 'mM' and token[:-1].isdigit():
        return token.lower()
    hz = token[:-1] if token[-1:] in 'Ll' else token
    if not hz.isdigit():
        raise SystemExit(f'Unsupported flash frequency {value!r}')
    return f'{int(hz) // 1000000}m'


def flash_settings(ini_text, envs=ENVS):
    """Return (mode, freq, size) shared by every board environment.

    Read board_build.flash_mode itself. pioarduino's _get_board_flash_mode()
    rewrites qio and qout to dio before elf2image, and the qio_opi bootloader
    has CONFIG_ESPTOOLPY_FLASH_MODE_AUTO_DETECT, so it keeps that header.
    The merged image has to follow the ini, not that downgrade.
    """
    sections = ini_sections(ini_text)
    resolved = []
    for env in envs:
        section = env if str(env).startswith('env:') else f'env:{env}'
        mode = lookup(sections, section, 'board_build.flash_mode')
        size = lookup(sections, section, 'board_upload.flash_size')
        freq = flash_freq_token(
            lookup(sections, section, 'board_build.f_flash')
            or lookup(sections, section, 'board_build.f_image'))
        if mode not in FLASH_MODE_BYTE:
            raise SystemExit(f'{section} flash mode {mode!r} is not one of {", ".join(FLASH_MODE_BYTE)}')
        if size not in FLASH_SIZE_HIGH:
            raise SystemExit(f'{section} flash size {size!r} is not one of {", ".join(FLASH_SIZE_HIGH)}')
        if freq not in FLASH_FREQ_LOW:
            raise SystemExit(f'{section} flash frequency {freq!r} is not one of {", ".join(FLASH_FREQ_LOW)}')
        resolved.append((mode, freq, size))
    if len(set(resolved)) != 1:
        raise SystemExit(f'Board flash settings differ: {dict(zip(envs, resolved))}')
    return resolved[0]


def header_bytes(mode, freq, size):
    return FLASH_MODE_BYTE[mode], FLASH_SIZE_HIGH[size] | FLASH_FREQ_LOW[freq]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--core-dir', default=os.environ.get('PLATFORMIO_CORE_DIR', str(Path.home()/'.platformio')))
    args = parser.parse_args()
    core = Path(args.core_dir)
    esptool = core/'packages/tool-esptoolpy/esptool.py'
    boot_app = core/'packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin'
    mode, freq, size = flash_settings((root/'platformio.ini').read_text())
    mode_byte, size_freq_byte = header_bytes(mode, freq, size)
    metadata = {
        'version': '1.0.0',
        'commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip(),
        'flash': {'mode': mode, 'freq': freq, 'size': size, 'modeByte': mode_byte, 'sizeFreqByte': size_freq_byte},
        'boards': {},
    }
    for board in BOARDS:
        build = root/'.pio/build'/f'esp32-s3-amoled-{board}'
        target = root/'web/flash'/f'firmware-{board}.bin'
        parts = [(0, build/'bootloader.bin'), (0x8000, build/'partitions.bin'), (0xe000, boot_app), (0x10000, build/'firmware.bin')]
        for index, (offset, path) in enumerate(parts):
            if not path.is_file():
                raise SystemExit(f'Missing {path}; build both PlatformIO environments first.')
            if index + 1 < len(parts) and offset + path.stat().st_size > parts[index + 1][0]:
                raise SystemExit(f'Overlapping flash part: {path}')
        python = core/'penv/bin/python'
        command = [str(python) if python.exists() else sys.executable, str(esptool), '--chip', 'esp32s3', 'merge_bin',
                   '-o', str(target), '--flash_mode', mode, '--flash_freq', freq, '--flash_size', size]
        for offset, path in parts:
            command.extend([hex(offset), str(path)])
        subprocess.run(command, check=True)
        image = target.read_bytes()
        # esptool updates the boot header's flash parameters; all other regions must match.
        for offset, path in parts[1:]:
            raw = path.read_bytes()
            assert image[offset:offset + len(raw)] == raw
        assert image[0] == 0xe9 and image[0x10000] == 0xe9
        assert image[2] == mode_byte and image[3] == size_freq_byte
        app_mode = image[0x10000 + 2]
        assert app_mode in FLASH_MODE_BYTE.values()
        metadata['boards'][board] = {
            'file': target.name, 'bytes': len(image), 'sha256': hashlib.sha256(image).hexdigest(),
            'bootFlashMode': image[2], 'appFlashMode': app_mode,
        }
        print(f'{board}: boot flash mode byte {image[2]}, app flash mode byte {app_mode}')
    (root/'web/flash/build.json').write_text(json.dumps(metadata, indent=2) + '\n')
    preview = root/'build/globe.png'
    if not preview.exists():
        raise SystemExit('Run python3 tools/test_host.py before packaging to generate the globe preview.')
    shutil.copyfile(preview, root/'web/flash/globe.png')
    print(f'Packaged both images with flash mode {mode} ({freq}, {size}).')


if __name__ == '__main__':
    main()
