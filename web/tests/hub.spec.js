import { test, expect } from '@playwright/test';
import { createHash } from 'node:crypto';
const checksum = (data) => ({ sha256: createHash('sha256').update(data).digest('hex'), size: Buffer.byteLength(data) });
const binary = Buffer.from('firmware test fixture');
const manifest = JSON.stringify({ name: 'Aegis-TX', version: '0.2.0-beta.1', new_install_prompt_erase: true,
  builds: [{ chipFamily: 'ESP32', parts: [{ path: 'app.bin', offset: 65536 }] }] });
const entry = { version: '0.2.0-beta.1', channel: 'beta', board: 'aegis-tx-esp32', hardware_revision: 'tx01',
  commit: 'a'.repeat(40), hardware_tested: false, published_at: '2026-10-01T00:00:00Z', notes: '<script>throw 1</script>',
  release_url: 'https://github.com/vinhphannn/Aegis-TX/releases/tag/v0.2.0-beta.1',
  manifest: 'firmware/manifest.json', metadata: 'firmware/firmware.json', download: 'firmware/Aegis-TX_Firmware.zip' };
const metadata = { schema_version: 1, ...entry, manifest: 'manifest.json', files: { 'manifest.json': checksum(manifest), 'app.bin': checksum(binary) } };
async function setup(page, { broken = false, mockFlasher = false } = {}) {
  await page.addInitScript(() => { if (!('serial' in navigator)) Object.defineProperty(navigator, 'serial', { value: {} }); });
  await page.route('**/catalog.json', route => route.fulfill({ json: { schema_version: 1, tx: [entry], fc: [] } }));
  await page.route('**/firmware/firmware.json', route => route.fulfill({ json: metadata }));
  await page.route('**/firmware/manifest.json', route => route.fulfill({ body: manifest }));
  await page.route('**/firmware/app.bin', route => route.fulfill({ body: broken ? Buffer.from('tampered') : binary }));
  if (mockFlasher) await page.route('**/vendor/flasher.js', route => route.fulfill({ contentType: 'text/javascript', body: "customElements.define('esp-web-install-button', class extends HTMLElement {});" }));
  await page.goto('/');
  await page.locator('#channel').selectOption('beta');
}
test('empty catalog and FC state are honest', async ({ page }) => {
  await page.goto('/');
  await expect(page.locator('#empty-state')).toBeVisible();
  await page.getByRole('button', { name: /Aegis FC/ }).click();
  await expect(page.locator('#fc-guide')).toBeVisible();
  await expect(page.locator('#empty-copy')).toContainText('1179');
  await expect(page.locator('#prepare')).toBeHidden();
});
test('requires board and beta confirmation, verifies bytes, then enables real bundled flasher', async ({ page }) => {
  await setup(page);
  await expect(page.locator('#prepare')).toBeDisabled();
  await page.locator('#board-confirm').check();
  await expect(page.locator('#prepare')).toBeDisabled();
  await page.locator('#beta-confirm').check();
  await page.locator('#prepare').click();
  await expect(page.locator('#flash-status')).toContainText('SHA-256 hợp lệ');
  await expect(page.locator('esp-web-install-button button[slot=activate]')).toBeEnabled();
  const result = await page.locator('esp-web-install-button').evaluate(async el => {
    const value = await (await fetch(el.manifest)).json();
    return { erase: value.new_install_prompt_erase, binary: await (await fetch(value.builds[0].parts[0].path)).text() };
  });
  expect(result).toEqual({ erase: true, binary: binary.toString() });
  await page.locator('#board-confirm').uncheck();
  await expect(page.locator('esp-web-install-button button[slot=activate]')).toBeDisabled();
  await page.locator('#board-confirm').check();
  await page.evaluate(() => { window.portRequests = 0; navigator.serial.requestPort = async () => { window.portRequests++; return null; }; });
  await page.locator('esp-web-install-button button[slot=activate]').click();
  await page.evaluate(() => customElements.whenDefined('ewt-install-dialog'));
  expect(await page.evaluate(() => window.portRequests)).toBe(1);
  await page.locator('#channel').selectOption('stable');
  await expect(page.locator('esp-web-install-button')).toHaveCount(0);
});
test('checksum failure never enables USB installer', async ({ page }) => {
  await setup(page, { broken: true });
  await page.locator('#board-confirm').check();
  await page.locator('#beta-confirm').check();
  await page.locator('#prepare').click();
  await expect(page.locator('#flash-status')).toContainText('Checksum không khớp');
  await expect(page.locator('esp-web-install-button')).toHaveCount(0);
});
test('stale download cannot install firmware after changing device', async ({ page }) => {
  await setup(page, { mockFlasher: true });
  let release;
  const pending = new Promise(resolve => { release = resolve; });
  await page.route('**/firmware/app.bin', async route => { await pending; await route.fulfill({ body: binary }); });
  await page.locator('#board-confirm').check();
  await page.locator('#beta-confirm').check();
  await page.locator('#prepare').click();
  await page.getByRole('button', { name: /Aegis FC/ }).click();
  release();
  await expect(page.locator('#device-title')).toHaveText('Aegis FC v1');
  await expect(page.locator('esp-web-install-button')).toHaveCount(0);
});
test('mobile layout fits and notes render as plain text', async ({ page }) => {
  await page.setViewportSize({ width: 390, height: 844 });
  await setup(page);
  await page.locator('summary').click();
  await expect(page.locator('#notes')).toHaveText(entry.notes);
  expect(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth)).toBe(true);
  await page.screenshot({ path: 'test-results/hub-mobile.png', fullPage: true });
});
test('catalog error is visible and disables firmware selection', async ({ page }) => {
  await page.route('**/catalog.json', route => route.fulfill({ status: 503 }));
  await page.goto('/');
  await expect(page.locator('#catalog-status')).toContainText('503');
  await expect(page.locator('#version')).toBeDisabled();
});
test('unsupported browser offers downloads without enabling installation', async ({ page }) => {
  await page.addInitScript(() => { delete Navigator.prototype.serial; });
  await page.route('**/catalog.json', route => route.fulfill({ json: { schema_version: 1, tx: [entry], fc: [] } }));
  await page.goto('/');
  await page.locator('#channel').selectOption('beta');
  await page.locator('#board-confirm').check();
  await page.locator('#beta-confirm').check();
  await expect(page.locator('#prepare')).toBeDisabled();
  await expect(page.locator('#browser-status')).toContainText('chưa hỗ trợ Web Serial');
  await expect(page.locator('#download')).toHaveAttribute('href', /Firmware.zip$/);
});
test('FC release downloads remain separate from USB flashing', async ({ page }) => {
  const fc = { version: 'v1.0.0', channel: 'stable', published_at: entry.published_at,
    release_url: 'https://github.com/vinhphannn/PX4-Autopilot/releases/tag/v1.0.0',
    download: 'firmware/fc/1/aegis_fc-v1_default.px4', sha256: 'b'.repeat(64), board_id: 1179 };
  await page.route('**/catalog.json', route => route.fulfill({ json: { schema_version: 1, tx: [], fc: [fc] } }));
  await page.goto('/');
  await page.getByRole('button', { name: /Aegis FC/ }).click();
  await expect(page.locator('#fc-download')).toHaveAttribute('href', /aegis_fc-v1_default.px4$/);
  await expect(page.locator('#fc-checksum')).toContainText(fc.sha256);
  await expect(page.locator('#tx-controls')).toBeHidden();
});
