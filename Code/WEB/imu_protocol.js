/* Raw-sample contract shared by recording and MQTT inference. No features here:
   Python imu_features.window_features remains the train/inference extractor. */
(function (root) {
  'use strict';
  function sampleFromMessage(message, node, context) {
    if (message.units?.accel !== 'm/s^2' || message.units?.gyro !== 'rad/s') {
      throw new Error('Firmware units missing or incompatible; update Gateway firmware');
    }
    for (const name of ['accel', 'gyro']) {
      if (!Array.isArray(node[name]) || node[name].length !== 3 || !node[name].every(Number.isFinite)) {
        throw new Error(`Missing finite ${name} axes; sample excluded instead of filling zeros`);
      }
    }
    if (!Number.isInteger(node.seq) || !Number.isFinite(node.ts) || node.ts < 0) {
      throw new Error('Invalid sensor sequence or device timestamp');
    }
    if (node.valid !== true || node.isMock === true) throw new Error('Waiting for a valid real BLE sample');
    const site = node.mount_site || context.mount_site;
    const body = node.body_part || context.body_part;
    if (site !== context.mount_site || body !== context.body_part) {
      throw new Error(`Selected context differs from Gateway: ${site}/${body}; configure the node or selection`);
    }
    const side = node.side || 'unknown';
    if (context.side !== 'unknown' && side !== context.side) throw new Error('Selected side differs from Gateway');
    return {
      sensor_uid: node.sensor_uid || `${message.gateway?.clientId || 'gateway'}:node_${node.nodeId}`,
      mount_site: site, body_part: body, side,
      stream_epoch: `${message.gateway?.bootId ?? 0}:${node.boot_id ?? 0}`,
      seq: node.seq, boot_ms: node.ts, device_time_s: node.ts / 1000,
      accel_x: node.accel[0], accel_y: node.accel[1], accel_z: node.accel[2],
      gyro_x: node.gyro[0], gyro_y: node.gyro[1], gyro_z: node.gyro[2],
      roll_deg: node.roll, pitch_deg: node.pitch, yaw_deg: node.yaw,
      calibrated: node.calibrated === true,
      relative_orientation_valid: node.relative_orientation_valid === true,
      calibration_id: node.calibration_id || '',
      calibration_method_version: node.calibration_method_version || 0,
      valid: true, firmware_status: 'real_ble'
    };
  }
  class SampleWindow {
    constructor(seconds = 1, gap = 0.15) { this.seconds = seconds; this.gap = gap; this.rows = []; }
    reset() { this.rows = []; }
    add(row) {
      const prev = this.rows.at(-1);
      const identity = ['sensor_uid', 'stream_epoch', 'mount_site', 'body_part', 'side', 'calibration_id',
                        'calibration_method_version', 'calibrated', 'relative_orientation_valid'];
      const same = prev && identity.every(k => prev[k] === row[k]);
      if (same && prev.seq === row.seq) return false;
      if (prev && (!same || row.seq !== prev.seq + 1 || row.device_time_s <= prev.device_time_s ||
          row.device_time_s - prev.device_time_s > this.gap)) this.reset();
      this.rows.push(row);
      while (this.rows.length > 3 && row.device_time_s - this.rows[1].device_time_s >= this.seconds) this.rows.shift();
      return true;
    }
    ready() { return this.rows.length >= 3 && this.rows.at(-1).device_time_s - this.rows[0].device_time_s + 1e-9 >= this.seconds; }
  }
  // The firmware's legacy "back" name means the lumbar context in pose_labels.json.
  function trainingBody(node) {
    return node.body_part === 'back' && node.mount_site === 'lumbar' ? 'lumbar' : node.body_part;
  }
  function nodeKey(message, node) {
    return JSON.stringify([message.gateway?.clientId, node.nodeId, node.sensor_uid,
      node.mount_site, node.body_part, node.side || 'unknown']);
  }
  const CSV_COLUMNS = [
    'gateway_id', 'gateway_boot_id', 'mqtt_seq', 'node_id', 'sensor_uid',
    'mount_site', 'body_part', 'firmware_body_part', 'side', 'subject_id', 'session_id', 'trial_id',
    'action_label', 'phase_label', 'phase_annotation_source', 'phase_annotation_reviewed',
    'host_time_iso', 'host_time_s', 'host_elapsed_ms', 'boot_ms', 'device_time_s',
    'boot_id', 'seq', 'stream_epoch', 'gap_before', 'missing_samples_before',
    'accel_unit', 'gyro_unit', 'accel_x', 'accel_y', 'accel_z', 'gyro_x', 'gyro_y', 'gyro_z',
    'roll_deg', 'pitch_deg', 'yaw_deg', 'calibrated', 'relative_orientation_valid',
    'calibration_id', 'calibration_method_version', 'valid', 'firmware_status'
  ];
  function csv(rows) {
    const quote = value => /[",\r\n]/.test(String(value ?? ''))
      ? '"' + String(value).replace(/"/g, '""') + '"' : String(value ?? '');
    return '\uFEFF' + [CSV_COLUMNS.join(','), ...rows.map(row =>
      CSV_COLUMNS.map(key => quote(row[key])).join(','))].join('\r\n');
  }
  class MultiNodeRecorder {
    constructor() { this.active = false; this.streams = new Map(); }
    start(gatewayId, metadata, now = Date.now()) {
      if (!gatewayId) throw new Error('Select a Gateway before recording');
      this.gatewayId = gatewayId;
      this.metadata = {...metadata};
      this.startedAt = now;
      this.stoppedAt = null;
      this.streams.clear();
      this.active = true;
    }
    ingest(message, labels, annotation, now = Date.now()) {
      if (!this.active || message.gateway?.clientId !== this.gatewayId) return;
      for (const node of message.nodes || []) {
        const key = nodeKey(message, node);
        let stream = this.streams.get(key);
        if (!stream) {
          stream = {key, nodeId: node.nodeId, sensorUid: node.sensor_uid,
            mountSite: node.mount_site, bodyPart: trainingBody(node), side: node.side || 'unknown',
            rows: [], window: new SampleWindow(), duplicates: 0, invalid: 0,
            outOfOrder: 0, missing: 0, breaks: 0, gapPending: false, lastError: ''};
          this.streams.set(key, stream);
        }
        // Accept optional batches as well as the existing one-sample-per-node v2 stream.
        const samples = Array.isArray(node.samples) ? node.samples : [node];
        for (const sample of samples) {
          const source = {...node, ...sample};
          let row;
          try {
            if (!source.sensor_uid || !source.mount_site || !source.body_part) {
              throw new Error('Missing firmware sensor identity or mounting context');
            }
            row = sampleFromMessage(message, source, {
              mount_site: node.mount_site, body_part: node.body_part, side: node.side || 'unknown'
            });
          } catch (error) {
            stream.invalid++; stream.gapPending = true; stream.lastError = error.message;
            continue;
          }
          row.stream_epoch = `${this.gatewayId}:${row.stream_epoch}`;
          const previous = stream.window.rows.at(-1);
          const sameEpoch = previous && previous.stream_epoch === row.stream_epoch;
          if (sameEpoch && previous.seq === row.seq) { stream.duplicates++; continue; }
          const delta = previous ? (row.seq - previous.seq + 65536) % 65536 : 1;
          if (sameEpoch && (row.device_time_s <= previous.device_time_s || delta > 32768)) {
            stream.outOfOrder++; continue;
          }
          const missing = sameEpoch && delta > 1 ? delta - 1 : 0;
          const gap = stream.gapPending || (!!previous && (!sameEpoch || delta !== 1 ||
            row.device_time_s - previous.device_time_s > stream.window.gap));
          stream.window.add(row);
          stream.missing += missing;
          if (gap) stream.breaks++;
          stream.gapPending = false;
          stream.lastError = '';
          const label = labels(key, source) || '';
          stream.rows.push({...row, ...this.metadata,
            gateway_id: this.gatewayId, gateway_boot_id: message.gateway.bootId,
            mqtt_seq: message.gateway.mqttSeq, node_id: node.nodeId, boot_id: source.boot_id,
            body_part: trainingBody(source), firmware_body_part: source.body_part,
            action_label: label, phase_label: annotation.phase,
            phase_annotation_source: annotation.source,
            phase_annotation_reviewed: annotation.reviewed === true,
            host_time_iso: new Date(now).toISOString(), host_time_s: now / 1000,
            host_elapsed_ms: now - this.startedAt, gap_before: gap,
            missing_samples_before: missing, accel_unit: 'm/s^2', gyro_unit: 'rad/s'});
        }
      }
    }
    stop(now = Date.now()) { this.active = false; this.stoppedAt = now; }
    manifest() {
      return {schema: 'planko-multinode-capture-v1', transport: 'MQTT',
        broker: 'wss://test.mosquitto.org:8081/mqtt', topic: 'planko/imu',
        gateway_id: this.gatewayId, ...this.metadata,
        started_at: new Date(this.startedAt).toISOString(),
        stopped_at: this.stoppedAt === null ? null : new Date(this.stoppedAt).toISOString(),
        target_sampling_hz: 20,
        units: {accel: 'm/s^2', gyro: 'rad/s', orientation: 'degrees'},
        note: 'Samples are forwarded in per-node batches. MQTT QoS 0 cannot confirm delivery across broker disconnects; observed sequence gaps are reported. Device clocks are independent across nodes.',
        streams: [...this.streams.values()].map(s => ({node_id: s.nodeId, sensor_uid: s.sensorUid,
          mount_site: s.mountSite, body_part: s.bodyPart, side: s.side, samples: s.rows.length,
          duplicates_excluded: s.duplicates, invalid_excluded: s.invalid,
          out_of_order_excluded: s.outOfOrder, observed_missing_samples: s.missing,
          discontinuities: s.breaks, unlabeled_samples: s.rows.filter(r => !r.action_label).length,
          phase_reviewed_samples: s.rows.filter(r => r.phase_annotation_reviewed).length}))};
    }
  }
  const api = {sampleFromMessage, SampleWindow, MultiNodeRecorder, nodeKey, trainingBody, CSV_COLUMNS, csv};
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.PlanKOImu = api;
})(globalThis);
