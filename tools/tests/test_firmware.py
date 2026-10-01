import base64
import json
import importlib.util
import shutil
import struct
import sys
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from firmware import package, json_write, prepare, safe_path, normalize_bootloader
from build_hub import import_tx, import_fc


class Packages(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.build = self.root / 'build'
        (self.build / 'bootloader').mkdir(parents=True)
        (self.build / 'partition_table').mkdir()
        self.request = self.root / 'request.json'
        json_write(self.request, prepare('v0.2.0-beta.1', 'beta', 'a' * 40))
        json_write(self.build / 'project_description.json', dict(project_version='0.2.0-beta.1', target='esp32'))
        self.args = {
            'flash_settings': {'flash_mode': 'dio', 'flash_size': '2MB', 'flash_freq': '40m'},
            'extra_esptool_args': {'chip': 'esp32'},
            'flash_files': {'0x1000': 'bootloader/bootloader.bin', '0x8000': 'partition_table/partition-table.bin', '0x10000': 'Aegis-TX.bin'},
            'bootloader': {'offset': '0x1000', 'file': 'bootloader/bootloader.bin'},
            'partition-table': {'offset': '0x8000', 'file': 'partition_table/partition-table.bin'},
            'app': {'offset': '0x10000', 'file': 'Aegis-TX.bin'}}
        json_write(self.build / 'flasher_args.json', self.args)
        (self.build / 'bootloader/bootloader.bin').write_bytes(b'bootloader')
        (self.build / 'Aegis-TX.bin').write_bytes(b'application')
        def partition(kind, offset, size, label):
            return struct.pack('<HBBII16sI', 0x50aa, kind, 0, offset, size, label, 0)
        (self.build / 'partition_table/partition-table.bin').write_bytes(
            partition(1, 0x9000, 0x6000, b'nvs') + partition(0, 0x10000, 0x100000, b'factory') + b'\xff' * 32)
        self.release = dict(tag_name='v0.2.0-beta.1', prerelease=True, published_at='2026-10-01', body='Notes', html_url='https://github.com/vinhphannn/Aegis-TX/releases/tag/v0.2.0-beta.1')

    def bundle(self):
        return package(self.build, self.root / 'package', self.request, normalize=lambda *args: None)

    def test_roundtrip_preserves_paths_and_offsets(self):
        archive = self.bundle()
        dest = self.root / 'site'
        entry = import_tx(archive, dest, self.release)
        self.assertEqual(entry['version'], '0.2.0-beta.1')
        manifest = json.loads((dest / 'manifest.json').read_text())
        self.assertTrue(manifest['new_install_prompt_erase'])
        self.assertEqual([p['offset'] for p in manifest['builds'][0]['parts']], [0x1000, 0x8000, 0x10000])
        for name in self.args['flash_files'].values():
            self.assertTrue((dest / name).is_file())

    @unittest.skipUnless(importlib.util.find_spec('esptool'), 'requires ESP-IDF esptool')
    def test_real_bootloader_normalization_keeps_segment_size(self):
        source = Path(__file__).resolve().parents[2] / 'stable_checkpoint/bootloader.bin'
        target = self.root / 'real-bootloader.bin'
        shutil.copyfile(source, target)
        normalize_bootloader(target, 0x1000, self.args['flash_settings'])
        self.assertEqual(target.stat().st_size, source.stat().st_size)
        self.assertEqual(target.read_bytes()[0], 0xe9)
        self.assertEqual(target.read_bytes()[2:4], bytes([2, 0x10]))  # DIO, 2 MB, 40 MHz

    def test_missing_binary_rejected(self):
        (self.build / 'Aegis-TX.bin').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing'): self.bundle()

    def test_data_partition_write_rejected(self):
        self.args['flash_files']['0x9000'] = 'nvs.bin'
        (self.build / 'nvs.bin').write_bytes(b'bad')
        json_write(self.build / 'flasher_args.json', self.args)
        with self.assertRaisesRegex(ValueError, 'data partition'): self.bundle()

    def test_oversized_app_rejected(self):
        (self.build / 'Aegis-TX.bin').write_bytes(b'x' * (0x100000 + 1))
        with self.assertRaisesRegex(ValueError, 'fit its partition'): self.bundle()

    def test_wrong_build_version_rejected(self):
        json_write(self.build / 'project_description.json', dict(project_version='wrong', target='esp32'))
        with self.assertRaisesRegex(ValueError, 'version or target'): self.bundle()

    def test_checksum_tampering_rejected(self):
        archive = self.bundle()
        altered = self.root / 'altered.zip'
        with zipfile.ZipFile(archive) as source, zipfile.ZipFile(altered, 'w') as dest:
            for name in source.namelist():
                dest.writestr(name, b'tampered' if name == 'Aegis-TX.bin' else source.read(name))
        with self.assertRaisesRegex(ValueError, 'Checksum'): import_tx(altered, self.root / 'bad', self.release)

    def test_release_channel_mismatch_rejected(self):
        archive = self.bundle()
        self.release['prerelease'] = False
        with self.assertRaisesRegex(ValueError, 'channel/tag'): import_tx(archive, self.root / 'bad', self.release)

    def test_legacy_archive_not_flashable(self):
        archive = self.root / 'legacy.zip'
        with zipfile.ZipFile(archive, 'w') as z: z.writestr('TX01.bin', b'old')
        self.assertIsNone(import_tx(archive, self.root / 'old', self.release))

    def test_path_traversal_rejected(self):
        for name in ('../outside', '/tmp/outside', 'a/../../outside', 'C:\\outside'):
            with self.assertRaises(ValueError): safe_path(self.root, name)

    def test_version_and_stable_gate(self):
        for version, channel, tested in [('v1.0.0', 'stable', False), ('v1.0.0-beta.1', 'stable', True), ('v1.0.0;echo hi', 'beta', False), ('v01.0.0', 'stable', True)]:
            with self.assertRaises(ValueError): prepare(version, channel, 'a' * 40, tested)
        self.assertEqual(prepare('v1.0.0', 'stable', 'a' * 40, True)['version'], '1.0.0')

    def test_fc_board_and_decompressed_size(self):
        path = self.root / 'aegis_fc-v1_default.px4'
        payload = dict(magic='PX4FWv1', board_id=1179, image_size=4, image=base64.b64encode(zlib.compress(b'test')).decode())
        json_write(path, payload)
        self.assertEqual(import_fc(path, self.release)['board_id'], 1179)
        payload['board_id'] = 9
        json_write(path, payload)
        with self.assertRaisesRegex(ValueError, 'board 1179'): import_fc(path, self.release)
        payload['board_id'] = 1179
        payload['image_size'] = 3
        json_write(path, payload)
        with self.assertRaisesRegex(ValueError, 'compressed image'): import_fc(path, self.release)


if __name__ == '__main__': unittest.main()
