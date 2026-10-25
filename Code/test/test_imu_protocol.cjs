const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const {sampleFromMessage, SampleWindow} = require('../WEB/imu_protocol.js');
const context = {mount_site: 'wrist', body_part: 'wrist', side: 'unknown'};
const message = {units: {accel: 'm/s^2', gyro: 'rad/s'}, gateway: {clientId: 'gw', bootId: 10}};
const rows = Array.from({length: 21}, (_, i) => sampleFromMessage(message, {
  nodeId: 1, sensor_uid: 'sensor-1', mount_site: 'wrist', body_part: 'wrist', side: 'unknown',
  boot_id: 20, seq: i, ts: 5000+i*50, accel: [0.1+i/100, 0.2, 9.80665],
  gyro: [0.01, 0.02+i/1000, 0.03], roll: 1, pitch: 2, yaw: 3,
  valid: true, calibrated: true, relative_orientation_valid: false
}, context));
if (process.argv.includes('--emit')) {
  process.stdout.write(JSON.stringify(rows));
} else {
  const window = new SampleWindow();
  rows.forEach(row => window.add(row));
  assert.equal(window.ready(), true);
  assert.equal(window.rows.length, 21);
  assert.equal(window.add(rows.at(-1)), false);
  window.add({...rows.at(-1), seq: 22, device_time_s: 6.1});
  assert.equal(window.ready(), false);
  window.add({...rows[0], stream_epoch: 'new-boot'});
  assert.equal(window.rows.length, 1);
  const node = {seq: 0, ts: 0, valid: true, accel: [0, 0, 9.8]};
  assert.throws(() => sampleFromMessage(message, node, context), /gyro/);
  assert.throws(() => sampleFromMessage({...message, units: {}}, {...node, gyro: [0,0,0]}, context), /units/);
  assert.throws(() => sampleFromMessage(message, {...node, gyro:[0,0,0], mount_site:'neck'}, context), /context/);
  const html = fs.readFileSync(require('node:path').join(__dirname, '../WEB/index.html'), 'utf8');
  for (const m of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)) if (m[1].trim()) new vm.Script(m[1]);
  assert.ok(!html.includes('/api/live?'), 'Monitor must use MQTT samples, not sensor HTTP');
  assert.ok(html.includes('/api/predict-window'));
  console.log('PASS: MQTT SI units, six axes, zero sequence, windows, duplicate/gap/boot reset, inline JS and MQTT-only data source');
}
