# Aegis-TX releases and firmware catalog

Configurator: https://vinhphannn.github.io/aegis-web/configurator/

Frontend source: https://github.com/vinhphannn/aegis-web

Catalog: https://vinhphannn.github.io/Aegis-TX/catalog.json

## Publish a version

1. Merge the desired source into `main`; require **CI Build Check** before releasing.
2. Run **Release Firmware** from Actions on `main`.
3. Set an explicit version such as `v0.2.0-beta.1`, with channel `beta`.
4. For `stable`, use a version without the beta suffix and confirm that sticks, calibration, BLE and radio have been tested on the intended hardware. A successful build alone does not qualify.
5. The workflow sets `version.txt` before compilation, validates the package, then uploads it to a draft release and publishes the completed release. Duplicate versions/tags are rejected. If upload/publication fails, inspect the draft and tag before retrying; nothing is overwritten automatically.
6. **Publish Firmware Catalog** rebuilds the catalog after a successful release. Run it manually after deleting/unpublishing a release; a daily refresh also synchronizes the catalog.

The first Hub-compatible board profile is `aegis-tx-esp32`, hardware revision `tx01`, corresponding to the existing ESP32 design. Do not reuse this profile for different pin mappings or new hardware revisions. Create a new profile and selector before shipping another board.

## Package format (schema 1)

`Aegis-TX_Firmware.zip` includes the original directory structure referenced by `flasher_args.json`, a web `manifest.json`, `firmware.json`, and `SHA256SUMS`. Metadata records the source commit, version, channel, board/revision, ESP-IDF version, flash settings, partitions, sizes and SHA-256 of each part. These checksums detect damaged/mismatched downloads; they are not a firmware signature or a replacement for trusted release access.

`tools/firmware.py` reads offsets from the ESP-IDF build, checks file existence, version/target, flash limits, sector overlaps and application partition bounds. It rejects writes to data partitions. The bootloader header is normalized with ESP-IDF's esptool `merge_bin --target-offset` using the actual build flash settings. Only that bootloader segment is normalized; **do not concatenate the entire flash map**, because padding can overwrite NVS between the partition table and application. The web manifest retains separate segments.

The installer asks before erasing the device (`new_install_prompt_erase: true`). Leave **Erase device** unchecked to keep data partitions. Keeping bytes does not guarantee data compatibility when downgrading or changing partition layouts. Erasing loses model/calibration data. The first implementation does not back up NVS through the browser or automatically identify the TX board revision; the user confirms the hardware explicitly.

The Configurator verifies bytes before creating the USB installer, then hands the verified bytes to ESP Web Tools through Blob URLs. The pinned installer dependency is hosted by aegis-web; firmware files are hosted by this repository. No runtime CDN or GitHub API request is required from the browser. The installer handles USB, flashing progress and write errors. SHA-256 verifies downloaded bytes before writing; this integration does not perform a separate full readback of device flash. Hardware flashing, data retention and recovery still require testing on the actual TX.

Legacy releases without schema-1 metadata remain available on GitHub and are not guessed into the web installer. CI development packages remain artifacts and are not published to the Hub catalog.

## Aegis FC

The catalog publisher also scans `vinhphannn/PX4-Autopilot` releases for assets named `aegis_fc-v1*.px4`. It checks PX4 magic, board ID **1179**, decompressed image length and the current board's 1,835,008-byte maximum, then mirrors the file and exposes its SHA-256. Other PX4 targets are not listed. With no matching releases, the FC panel explicitly shows an empty state.

Direct FC flashing is not implemented. Use QGroundControl's custom firmware flow. The future browser adapter must implement and test the PX4 bootloader protocol and board checks. No FC build/release pipeline or PX4 target is changed by this implementation.

## Local checks

```sh
python3 -m unittest discover -s tools/tests -v
```

To package a local development build (requires ESP-IDF v5.1.6):

```sh
python3 tools/prepare_release.py --development
idf.py build
python3 tools/firmware.py
```

`tools/build_catalog.py` downloads and validates published TX/FC releases, then writes `site/catalog.json` and `site/firmware/`. Run it with an authenticated `gh`; use a fresh `--output` directory for each run. The publisher only needs Python and GitHub CLI. Node, React, ESP Web Tools and browser tests live in `aegis-web`.

GitHub Pages uses GitHub Actions as its source and allows deployments from `main`. The release-completion workflow and daily refresh update firmware independently of the frontend. No cross-repository token or website rebuild is needed for a firmware release. The old Hub index redirects to the Configurator; existing catalog and firmware URLs stay valid. Pages serves these public files with CORS enabled.

Change `FC_REPO` in `tools/build_catalog.py` if the FC release repository moves.
