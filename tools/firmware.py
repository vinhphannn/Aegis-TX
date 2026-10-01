"""Validated, self-contained ESP32 firmware packages for releases and Aegis Hub."""
import argparse
import hashlib
import json
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path, PurePosixPath

BOARD = 'aegis-tx-esp32'
HARDWARE = 'tx01'
ASSET = 'Aegis-TX_Firmware.zip'
VERSION = re.compile(r'^v?(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(-beta\.[1-9]\d*)?$')


def json_write(path, value):
    Path(path).write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n')


def safe_path(root, name):
    p = PurePosixPath(name)
    if not name or p.is_absolute() or '..' in p.parts or '\\' in name or ':' in name:
        raise ValueError(f'Unsafe package path: {name}')
    target = (Path(root) / str(p)).resolve()
    if not target.is_relative_to(Path(root).resolve()):
        raise ValueError(f'Path escapes package: {name}')
    return target


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def release_version(version, channel, tested=False):
    if not VERSION.fullmatch(version) or len(version.removeprefix('v')) > 31:
        raise ValueError('Use vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-beta.N (max 31 bytes without v)')
    if (channel == 'beta') != ('-beta.' in version) or channel not in ('stable', 'beta'):
        raise ValueError('Version suffix must match channel')
    if channel == 'stable' and not tested:
        raise ValueError('Stable requires hardware test confirmation')
    return version.removeprefix('v')


def prepare(version, channel, commit, tested=False):
    version = release_version(version, channel, tested)
    if not re.fullmatch(r'[0-9a-f]{40}', commit):
        raise ValueError('Expected full Git commit SHA')
    return dict(version=version, channel=channel, commit=commit, hardware_tested=tested)


def normalize_bootloader(path, offset, settings):
    """Patch flash header with esptool, without filling gaps containing NVS."""
    with tempfile.TemporaryDirectory() as tmp:
        output = Path(tmp) / 'bootloader.bin'
        subprocess.run([sys.executable, '-m', 'esptool', '--chip', 'esp32', 'merge_bin',
                        '-o', str(output), '--target-offset', hex(offset),
                        '--flash_mode', settings['flash_mode'],
                        '--flash_freq', settings['flash_freq'],
                        '--flash_size', settings['flash_size'], hex(offset), str(path)], check=True)
        if output.stat().st_size != path.stat().st_size:
            raise ValueError('Normalized bootloader changed size')
        shutil.copyfile(output, path)


def partition_layout(path):
    result = []
    raw = path.read_bytes()
    for i in range(0, len(raw), 32):
        entry = raw[i:i + 32]
        if len(entry) < 32 or entry[:2] != b'\xaa\x50':
            break
        _, kind, subtype, offset, size, label, flags = struct.unpack('<HBBII16sI', entry)
        result.append(dict(type=kind, subtype=subtype, offset=offset, size=size,
                           label=label.rstrip(b'\0').decode('ascii')))
    if not result:
        raise ValueError('No valid ESP32 partitions')
    return result


def package(build, output, request, normalize=normalize_bootloader):
    build, output = Path(build), Path(output)
    info = json.loads(Path(request).read_text())
    if info['channel'] != 'development':
        release_version(info['version'], info['channel'], info['hardware_tested'])
    desc = json.loads((build / 'project_description.json').read_text())
    if desc['project_version'] != info['version'] or desc['target'] != 'esp32':
        raise ValueError('Build version or target differs from release request')
    args = json.loads((build / 'flasher_args.json').read_text())
    if args['extra_esptool_args']['chip'] != 'esp32':
        raise ValueError('Only the existing ESP32 TX board is supported')
    settings = args['flash_settings']
    if settings['flash_mode'] not in ('dio', 'dout') or not re.fullmatch(r'\d+MB', settings['flash_size']):
        raise ValueError('Web flashing requires explicit flash size and DIO/DOUT mode')
    flash_size = int(settings['flash_size'][:-2]) * 1024 * 1024
    output.mkdir(parents=True, exist_ok=False)
    parts = []
    for offset, name in args['flash_files'].items():
        source = safe_path(build, name)
        dest = safe_path(output, name)
        if not source.is_file() or not source.stat().st_size:
            raise ValueError(f'Missing firmware part: {name}')
        dest.parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, dest)
        parts.append(dict(path=name, offset=int(offset, 0)))
    parts.sort(key=lambda p: p['offset'])
    boot = args['bootloader']
    normalize(safe_path(output, boot['file']), int(boot['offset'], 0), settings)
    partitions = partition_layout(safe_path(output, args['partition-table']['file']))
    # Do not package data/OTA initializers: updating must not erase calibration/model data.
    protected = [p for p in partitions if p['type'] == 1]
    end = 0
    for part in parts:
        size = safe_path(output, part['path']).stat().st_size
        start = part['offset']
        sector_start, sector_end = start // 4096 * 4096, (start + size + 4095) // 4096 * 4096
        if start % 4096 or sector_start < end or sector_end > flash_size:
            raise ValueError('Invalid, overlapping or out-of-flash firmware region')
        if any(sector_start < p['offset'] + p['size'] and sector_end > p['offset'] for p in protected):
            raise ValueError('Firmware would overwrite a data partition')
        end = sector_end
    app = args['app']
    app_part = next((p for p in partitions if p['type'] == 0 and p['offset'] == int(app['offset'], 0)), None)
    if not app_part or safe_path(output, app['file']).stat().st_size > app_part['size']:
        raise ValueError('Application does not fit its partition')
    shutil.copyfile(build / 'flasher_args.json', output / 'flasher_args.json')
    manifest = dict(name='Aegis-TX', version=info['version'], new_install_prompt_erase=True,
                    new_install_improv_wait_time=0,
                    builds=[dict(chipFamily='ESP32', parts=parts)])
    json_write(output / 'manifest.json', manifest)
    info.update(schema_version=1, board=BOARD, hardware_revision=HARDWARE, chip='ESP32',
                esp_idf=desc.get('idf_ver', 'unknown'), flash_settings=settings,
                partitions=partitions, manifest='manifest.json',
                configuration_policy='preserve-data-partitions',
                files={p.relative_to(output).as_posix(): dict(sha256=digest(p), size=p.stat().st_size)
                       for p in sorted(output.rglob('*')) if p.is_file()})
    json_write(output / 'firmware.json', info)
    (output / 'SHA256SUMS').write_text(''.join(f'{digest(p)}  {p.relative_to(output).as_posix()}\n'
        for p in sorted(output.rglob('*')) if p.is_file() and p.name != 'SHA256SUMS'))
    archive = output.parent / ASSET
    with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED) as z:
        for path in sorted(output.rglob('*')):
            if path.is_file():
                z.write(path, path.relative_to(output).as_posix())
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', default='build')
    parser.add_argument('--output', default='dist/package')
    parser.add_argument('--request', default='release-request.json')
    opts = parser.parse_args()
    print(package(opts.build, opts.output, opts.request))
