const assert = require('node:assert/strict');
const {MultiNodeRecorder, nodeKey, csv} = require('../WEB/imu_protocol.js');

const base = 1800000000000;
function frame(i, gatewayId = 'gw-a') {
  return {type: 'imu_stream', schema: 'planko-imu-v2',
    units: {accel: 'm/s^2', gyro: 'rad/s'},
    gateway: {clientId: gatewayId, bootId: 100, mqttSeq: i},
    nodes: [
      {nodeId: 1, sensor_uid: 'neck-uid', mount_site: 'neck', body_part: 'neck'},
      {nodeId: 2, sensor_uid: 'lumbar-uid', mount_site: 'lumbar', body_part: 'back'}
    ].map((node, n) => ({...node, side: 'unknown', boot_id: 200 + n, seq: i,
      // Independent device clocks: both streams have 20 samples per second.
      ts: 10000 + n * 7000 + i * 50,
      accel: [n + i / 100, 0.2, 9.80665], gyro: [0.01, n / 10, 0.03],
      pitch: n * 20, roll: 1, yaw: 0, valid: true, isMock: false,
      calibrated: true, relative_orientation_valid: false}))};
}
const metadata = {subject_id: 'P01', session_id: 'S01', trial_id: 'trial-01'};
const annotation = {phase: 'neutral', source: 'guided_timer_cue', reviewed: false};
const label = (key, node) => node.nodeId === 1 ? 'neck_neutral' : 'lumbar_neutral';
const recorder = new MultiNodeRecorder();
recorder.start('gw-a', metadata, base);
for (let i = 0; i <= 40; i++) recorder.ingest(frame(i), label, annotation, base + i * 50);
const streams = [...recorder.streams.values()];
assert.equal(streams.length, 2);
for (const stream of streams) {
  assert.equal(stream.rows.length, 41);
  assert.equal(stream.rows.at(-1).device_time_s - stream.rows[0].device_time_s, 2);
  assert.equal(stream.rows.every(r => r.sensor_uid === stream.sensorUid), true);
  assert.equal(stream.rows.every(r => r.phase_annotation_reviewed === false), true);
  assert.equal(stream.missing, 0);
}
assert.equal(streams[1].rows[0].body_part, 'lumbar');
assert.equal(streams[1].rows[0].firmware_body_part, 'back');

if (process.argv.includes('--emit')) {
  recorder.stop(base + 2050);
  process.stdout.write(JSON.stringify(streams.map(s => ({name: `node${s.nodeId}.csv`, csv: csv(s.rows)}))));
} else {
  // Repeated latest-sample frames and other gateways must not contaminate capture.
  recorder.ingest(frame(40), label, annotation, base + 2050);
  recorder.ingest(frame(41, 'gw-b'), label, annotation, base + 2050);
  assert.equal(streams[0].rows.length, 41);
  assert.equal(streams[0].duplicates, 1);
  // One node loses a sample, while the other remains contiguous.
  const gap = frame(42);
  gap.nodes[1] = frame(41).nodes[1];
  recorder.ingest(gap, label, annotation, base + 2100);
  assert.equal(streams[0].rows.at(-1).gap_before, true);
  assert.equal(streams[0].missing, 1);
  assert.equal(streams[1].rows.at(-1).gap_before, false);
  recorder.ingest(frame(39), label, annotation, base + 2150);
  assert.equal(streams[0].outOfOrder, 1);
  // Missing gyro is excluded, never filled with zero; recovery is a boundary.
  const bad = frame(43);
  delete bad.nodes[0].gyro;
  recorder.ingest(bad, label, annotation, base + 2150);
  assert.equal(streams[0].invalid, 1);
  recorder.ingest(frame(44), label, annotation, base + 2200);
  assert.equal(streams[0].rows.at(-1).gap_before, true);
  assert.equal(streams[0].rows.at(-1).seq, 44);
  // A node reboot stays in its own file and starts a new epoch.
  const reboot = frame(0);
  reboot.nodes = [reboot.nodes[0]];
  reboot.nodes[0].boot_id++;
  recorder.ingest(reboot, label, annotation, base + 2250);
  assert.equal(streams[0].rows.at(-1).gap_before, true);
  assert.equal(streams[0].rows.at(-1).seq, 0);
  // A changed mounting context creates a different dataset rather than relabeling old rows.
  const moved = frame(1);
  moved.nodes = [{...moved.nodes[0], mount_site: 'wrist', body_part: 'wrist'}];
  recorder.ingest(moved, () => 'wrist_neutral', annotation, base + 2300);
  assert.equal(recorder.streams.size, 3);
  assert.equal(recorder.streams.get(nodeKey(moved, moved.nodes[0])).rows[0].body_part, 'wrist');
  const encoded = csv([{subject_id: 'P,"one"\nsecond line'}]);
  assert.ok(encoded.includes('"P,""one""\nsecond line"'));
  recorder.stop(base + 2350);
  const count = streams[0].rows.length;
  recorder.ingest(frame(45), label, annotation, base + 2400);
  assert.equal(streams[0].rows.length, count);
  const manifest = recorder.manifest();
  assert.equal(manifest.target_sampling_hz, 20);
  assert.equal(manifest.streams[0].observed_missing_samples, 2);
  assert.equal(manifest.streams[0].invalid_excluded, 1);
  console.log('PASS: simultaneous 20 Hz nodes, identity isolation, per-node labels, SI CSV, duplicate/out-of-order exclusion, gaps, reboot and context boundaries');
}
