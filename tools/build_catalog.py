"""Publish the firmware catalog and binaries consumed by aegis-web."""
import argparse
import base64
import json
import re
import shutil
import subprocess
import tempfile
import zipfile
import zlib
from pathlib import Path
from firmware import ASSET, BOARD, HARDWARE, digest, json_write, release_version, safe_path

TX_REPO = 'vinhphannn/Aegis-TX'
FC_REPO = 'vinhphannn/PX4-Autopilot'


def releases(repo):
    data = subprocess.check_output(['gh', 'api', '--paginate', '--slurp', f'repos/{repo}/releases?per_page=100'])
    return [r for page in json.loads(data) for r in page if not r['draft']]


def download(repo, release, asset, directory):
    subprocess.run(['gh', 'release', 'download', release['tag_name'], '--repo', repo,
                    '--pattern', asset['name'], '--dir', str(directory)], check=True)
    return Path(directory) / asset['name']


def import_tx(archive, dest, release):
    with zipfile.ZipFile(archive) as z:
        names = [i.filename for i in z.infolist()]
        if 'firmware.json' not in names:
            return None  # Legacy releases remain downloadable on GitHub; do not guess flash compatibility.
        if len(set(names)) != len(names) or sum(i.file_size for i in z.infolist()) > 32 * 1024 * 1024:
            raise ValueError('Invalid or oversized firmware archive')
        for name in names:
            safe_path(dest, name)
        z.extractall(dest)
    meta = json.loads((dest / 'firmware.json').read_text())
    if meta['schema_version'] != 1 or meta['board'] != BOARD or meta['hardware_revision'] != HARDWARE:
        raise ValueError('Unknown firmware board/schema')
    release_version(meta['version'], meta['channel'], meta['hardware_tested'])
    if release['tag_name'] != f"v{meta['version']}" or release['prerelease'] != (meta['channel'] == 'beta'):
        raise ValueError('Release channel/tag does not match binary metadata')
    if not re.fullmatch(r'[0-9a-f]{40}', meta['commit']):
        raise ValueError('Invalid source commit')
    for name, expected in meta['files'].items():
        p = safe_path(dest, name)
        if p.stat().st_size != expected['size'] or digest(p) != expected['sha256']:
            raise ValueError(f'Checksum mismatch: {name}')
    manifest = json.loads((dest / meta['manifest']).read_text())
    if meta['manifest'] not in meta['files'] or manifest['version'] != meta['version']:
        raise ValueError('Invalid manifest/version')
    if manifest.get('new_install_prompt_erase') is not True or len(manifest['builds']) != 1:
        raise ValueError('Unsafe erase policy or unsupported builds')
    build = manifest['builds'][0]
    if build['chipFamily'] != 'ESP32' or not build['parts']:
        raise ValueError('Unsupported chipset or empty firmware')
    for part in build['parts']:
        if part['path'] not in meta['files']:
            raise ValueError('Unverified manifest binary')
    args = json.loads((dest / 'flasher_args.json').read_text())
    expected_parts = sorted((int(offset, 0), name) for offset, name in args['flash_files'].items())
    if sorted((p['offset'], p['path']) for p in build['parts']) != expected_parts:
        raise ValueError('Flash mapping differs from build')
    checksum_lines = (dest / 'SHA256SUMS').read_text().splitlines()
    covered = set()
    for line in checksum_lines:
        sha, name = line.split('  ', 1)
        if digest(safe_path(dest, name)) != sha:
            raise ValueError(f'Invalid SHA256SUMS: {name}')
        covered.add(name)
    if covered != set(meta['files']) | {'firmware.json'}:
        raise ValueError('Incomplete checksums')
    if set(names) != covered | {'SHA256SUMS'}:
        raise ValueError('Unexpected package files')
    return dict(version=meta['version'], channel=meta['channel'], board=meta['board'],
                hardware_revision=meta['hardware_revision'], commit=meta['commit'],
                hardware_tested=meta['hardware_tested'], release_url=release['html_url'],
                published_at=release['published_at'], notes=release['body'] or '',
                manifest='manifest.json', metadata='firmware.json', download=ASSET)


def import_fc(path, release):
    data = json.loads(path.read_text())
    if data.get('magic') != 'PX4FWv1' or data.get('board_id') != 1179:
        raise ValueError('PX4 asset does not target Aegis FC v1 (board 1179)')
    size = data['image_size']
    if not isinstance(size, int) or not 0 < size <= 1835008:
        raise ValueError('PX4 image size exceeds Aegis FC flash')
    decoder = zlib.decompressobj()
    image = decoder.decompress(base64.b64decode(data['image'], validate=True), size + 1)
    if len(image) != size or not decoder.eof or decoder.unused_data:
        raise ValueError('Invalid PX4 compressed image')
    return dict(version=release['tag_name'], channel='beta' if release['prerelease'] else 'stable',
                release_url=release['html_url'], published_at=release['published_at'],
                sha256=digest(path), board_id=1179, notes=release['body'] or '')


def build_catalog(output, empty=False):
    Path(output).mkdir(parents=True, exist_ok=False)
    catalog = dict(schema_version=1, tx=[], fc=[])
    if not empty:
        for release in releases(TX_REPO):
            asset = next((a for a in release['assets'] if a['name'] == ASSET), None)
            if not asset:
                continue
            # Stable identifiers prevent path injection from arbitrary tag names.
            relative = f"firmware/tx/{release['id']}"
            dest = Path(output) / relative
            with tempfile.TemporaryDirectory() as temp:
                archive = download(TX_REPO, release, asset, temp)
                entry = import_tx(archive, dest, release)
                if entry:
                    shutil.copyfile(archive, dest / ASSET)
                    for field in ('manifest', 'metadata', 'download'):
                        entry[field] = f"{relative}/{entry[field]}"
                    catalog['tx'].append(entry)
        for release in releases(FC_REPO):
            for asset in release['assets']:
                if not re.fullmatch(r'aegis_fc-v1[\w.-]*\.px4', asset['name']):
                    continue
                with tempfile.TemporaryDirectory() as temp:
                    path = download(FC_REPO, release, asset, temp)
                    entry = import_fc(path, release)
                    relative = f"firmware/fc/{release['id']}/{path.name}"
                    dest = Path(output) / relative
                    dest.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(path, dest)
                    entry['download'] = relative
                    catalog['fc'].append(entry)
    for device in ('tx', 'fc'):
        catalog[device].sort(key=lambda x: x['published_at'], reverse=True)
    json_write(Path(output) / 'catalog.json', catalog)
    (Path(output) / '.nojekyll').touch()
    # Keep old bookmarks useful without maintaining a second website.
    (Path(output) / 'index.html').write_text(
        '<!doctype html><html lang="en"><meta charset="utf-8">'
        '<meta http-equiv="refresh" content="0;url=https://vinhphannn.github.io/aegis-web/configurator/">'
        '<title>AEGIS Configurator</title>'
        '<a href="https://vinhphannn.github.io/aegis-web/configurator/">Open AEGIS Configurator</a></html>')


if __name__ == '__main__':
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', default='site')
    p.add_argument('--empty', action='store_true', help='Create an empty catalog without downloading releases')
    args = p.parse_args()
    build_catalog(args.output, args.empty)
