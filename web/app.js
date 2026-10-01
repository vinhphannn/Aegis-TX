const $ = (id) => document.getElementById(id);
let catalog = { tx: [], fc: [] };
let device = 'tx';
let current;
let generation = 0;
let blobURLs = [];
let busy = false;
const supportsSerial = window.isSecureContext && 'serial' in navigator;

function resetFlash() {
  generation++;
  busy = false;
  $('install-host').replaceChildren();
  $('install-host').hidden = true;
  $('prepare').hidden = false;
  for (const url of blobURLs) URL.revokeObjectURL(url);
  blobURLs = [];
  $('flash-status').textContent = '';
  $('board-confirm').checked = false;
  $('beta-confirm').checked = false;
}
function gate() {
  const confirmed = $('board-confirm').checked && (current?.channel !== 'beta' || $('beta-confirm').checked);
  $('prepare').disabled = !current || !confirmed || !supportsSerial || busy;
  const activate = $('install-host').querySelector('button');
  if (activate) activate.disabled = !confirmed || busy;
}
function setDevice(next) {
  device = next;
  const tx = device === 'tx';
  $('tx-tab').classList.toggle('active', tx);
  $('fc-tab').classList.toggle('active', !tx);
  $('tx-tab').setAttribute('aria-pressed', String(tx));
  $('fc-tab').setAttribute('aria-pressed', String(!tx));
  $('device-title').textContent = tx ? 'Aegis-TX' : 'Aegis FC v1';
  $('device-description').textContent = tx ? 'ESP32 • Phần cứng TX01 hiện tại' : 'PX4 • Board ID 1179';
  $('capability').textContent = tx ? 'USB FLASH' : 'TẢI FIRMWARE';
  $('tx-guide').hidden = !tx;
  $('fc-guide').hidden = tx;
  $('all-releases').href = `https://github.com/vinhphannn/${tx ? 'Aegis-TX' : 'PX4-Autopilot'}/releases`;
  populate();
}
function populate() {
  resetFlash();
  const entries = catalog[device].filter((e) => e.channel === $('channel').value);
  $('version').replaceChildren(...entries.map((entry, i) => new Option(`v${entry.version.replace(/^v/, '')}`, String(i))));
  $('version').disabled = !entries.length;
  if (!entries.length) $('version').add(new Option('Chưa có phiên bản', ''));
  $('catalog-status').textContent = entries.length ? `${entries.length} phiên bản phù hợp với thiết bị này.` : '';
  current = entries[0];
  renderRelease();
}
function renderRelease() {
  resetFlash();
  $('release-detail').hidden = !current;
  $('empty-state').hidden = !!current;
  const beta = $('channel').value === 'beta';
  $('empty-title').textContent = `Chưa có firmware ${beta ? 'beta' : 'stable'}`;
  $('empty-copy').textContent = device === 'tx'
    ? `Chưa có gói ${beta ? 'beta' : 'stable'} tương thích với web flash. ${!beta && catalog.tx.some(e => e.channel === 'beta') ? 'Bạn có thể chọn kênh Beta để thử nghiệm.' : 'Các bản cũ vẫn có trên GitHub; bản mới sẽ xuất hiện sau khi được phát hành.'}`
    : 'Chưa có release chứa firmware Aegis FC v1. Khi có file .px4 đúng board ID 1179, Hub sẽ bổ sung vào danh sách tải.';
  if (!current) { gate(); return; }
  $('version-name').textContent = `v${current.version.replace(/^v/, '')}`;
  $('release-date').textContent = new Date(current.published_at).toLocaleDateString('vi-VN');
  $('release-link').href = githubURL(current.release_url);
  $('notes').textContent = current.notes || 'Chưa có ghi chú phát hành.';
  $('test-status').textContent = device === 'tx'
    ? (current.hardware_tested ? 'Nhà phát hành xác nhận đã thử phần cứng' : 'Chưa xác nhận thử trên phần cứng')
    : 'Đã đối chiếu board ID 1179 trong gói PX4';
  $('tx-controls').hidden = device !== 'tx';
  $('fc-controls').hidden = device !== 'fc';
  $('beta-confirm-row').hidden = !beta;
  if (device === 'tx') {
    $('download').href = localURL(current.download);
    $('browser-status').textContent = supportsSerial
      ? 'Kết nối USB chỉ bắt đầu khi bạn bấm nút và chọn cổng trên trình duyệt.'
      : 'Trình duyệt này chưa hỗ trợ Web Serial. Mở trang HTTPS bằng Chrome/Edge trên máy tính, hoặc tải ZIP để nạp thủ công.';
  } else {
    $('fc-download').href = localURL(current.download);
    $('fc-checksum').textContent = `SHA-256: ${current.sha256}`;
  }
  gate();
}
function localURL(path, base = document.baseURI) {
  const url = new URL(path, base);
  if (url.origin !== location.origin || !['https:', 'http:'].includes(url.protocol)) throw new Error('Đường dẫn firmware không hợp lệ.');
  return url.href;
}
function githubURL(path) {
  const url = new URL(path);
  if (url.origin !== 'https://github.com') throw new Error('Đường dẫn release không hợp lệ.');
  return url.href;
}
async function getJSON(url) {
  const response = await fetch(url, { cache: 'no-store' });
  if (!response.ok) throw new Error(`Không tải được dữ liệu (${response.status}).`);
  return response.json();
}
async function sha256(bytes) {
  return Array.from(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)), b => b.toString(16).padStart(2, '0')).join('');
}
async function prepareFlash() {
  if ($('prepare').disabled) return;
  const ticket = generation;
  const entry = current;
  const created = [];
  busy = true;
  gate();
  $('flash-status').textContent = 'Đang tải và kiểm tra SHA-256 của firmware…';
  try {
    const metadata = await getJSON(localURL(entry.metadata));
    if (metadata.schema_version !== 1 || metadata.board !== 'aegis-tx-esp32' || metadata.hardware_revision !== 'tx01'
        || metadata.version !== entry.version || metadata.commit !== entry.commit || metadata.channel !== entry.channel) {
      throw new Error('Firmware không khớp board hoặc phiên bản đã chọn.');
    }
    const manifestURL = localURL(entry.manifest);
    const loadChecked = async (name) => {
      const expected = metadata.files[name];
      if (!expected) throw new Error('Thiếu checksum trong gói firmware.');
      const response = await fetch(localURL(name, manifestURL), { cache: 'no-store' });
      if (!response.ok) throw new Error(`Không tải được firmware (${response.status}).`);
      const bytes = await response.arrayBuffer();
      if (bytes.byteLength !== expected.size || await sha256(bytes) !== expected.sha256) throw new Error('Checksum không khớp. Đã dừng trước khi kết nối thiết bị.');
      return bytes;
    };
    const manifest = JSON.parse(new TextDecoder().decode(await loadChecked(metadata.manifest)));
    if (manifest.version !== entry.version || manifest.new_install_prompt_erase !== true || manifest.builds.length !== 1
        || manifest.builds[0].chipFamily !== 'ESP32' || !manifest.builds[0].parts.length) throw new Error('Manifest firmware không hợp lệ.');
    for (const part of manifest.builds[0].parts) {
      const bytes = await loadChecked(part.path);
      const url = URL.createObjectURL(new Blob([bytes], { type: 'application/octet-stream' }));
      created.push(url);
      part.path = url;
    }
    // Bundle the flasher locally. It is only loaded when a user prepares an actual release.
    await import('./vendor/flasher.js');
    await customElements.whenDefined('esp-web-install-button');
    if (ticket !== generation) { for (const url of created) URL.revokeObjectURL(url); return; }
    const manifestBlob = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }));
    created.push(manifestBlob);
    blobURLs = created;
    const installer = document.createElement('esp-web-install-button');
    installer.manifest = manifestBlob;
    const button = document.createElement('button');
    button.slot = 'activate';
    button.className = 'primary';
    button.textContent = 'Kết nối USB & nạp firmware';
    installer.append(button);
    $('install-host').replaceChildren(installer);
    $('install-host').hidden = false;
    $('prepare').hidden = true;
    $('flash-status').textContent = 'SHA-256 hợp lệ. Sẵn sàng kết nối thiết bị. Chưa ghi firmware.';
  } catch (error) {
    for (const url of created) URL.revokeObjectURL(url);
    if (ticket === generation) $('flash-status').textContent = error.message || 'Không thể chuẩn bị firmware. Vui lòng thử lại.';
  } finally {
    if (ticket === generation) { busy = false; gate(); }
  }
}
$('tx-tab').addEventListener('click', () => setDevice('tx'));
$('fc-tab').addEventListener('click', () => setDevice('fc'));
$('channel').addEventListener('change', populate);
$('version').addEventListener('change', () => {
  current = catalog[device].filter(e => e.channel === $('channel').value)[Number($('version').value)];
  renderRelease();
});
$('board-confirm').addEventListener('change', gate);
$('beta-confirm').addEventListener('change', gate);
$('prepare').addEventListener('click', prepareFlash);
try {
  catalog = await getJSON('./catalog.json');
  if (catalog.schema_version !== 1 || !Array.isArray(catalog.tx) || !Array.isArray(catalog.fc)) throw new Error('Danh sách firmware không hợp lệ.');
  setDevice('tx');
} catch (error) {
  $('catalog-status').textContent = `${error.message} Tải lại trang để thử lại.`;
  $('version').replaceChildren(new Option('Không tải được dữ liệu', ''));
}
