// Run the actual page handlers with a minimal DOM and a local MQTT client double.
// No messages leave this process; rendering and a real broker are separate checks.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const protocol = require('../WEB/imu_protocol.js');
const html = fs.readFileSync(path.join(__dirname, '../WEB/index.html'), 'utf8');
class Element {
  constructor(tag) {
    this.tag = tag; this.children = []; this.listeners = new Map(); this.style = {};
    this.classList = {add() {}, remove() {}, toggle() {}};
    this.dataset = {}; this.disabled = false; this.checked = false; this._value = null;
  }
  get options() { return this.children.filter(e => e.tag === 'option'); }
  get value() { return this._value ?? this.options[0]?.value ?? ''; }
  set value(value) { this._value = value; }
  set innerHTML(value) { this.children = []; }
  appendChild(child) { this.children.push(child); return child; }
  append(...children) { this.children.push(...children); }
  prepend(...children) { this.children.unshift(...children); }
  replaceChildren(...children) { this.children = children; }
  setAttribute() {}
  addEventListener(name, handler) {
    if (!this.listeners.has(name)) this.listeners.set(name, []);
    this.listeners.get(name).push(handler);
  }
  fire(name) { for (const handler of this.listeners.get(name) || []) handler({target: this}); }
  click() { if (!this.disabled) this.fire('click'); }
  remove() {}
  getBoundingClientRect() { return {width: 800, height: 600}; }
  getContext() { return {}; }
}
const elements = new Map();
for (const match of html.matchAll(/<(\w+)\b([^>]*\bid="([^"]+)"[^>]*)>/g)) {
  const e = new Element(match[1]);
  const value = match[2].match(/\bvalue="([^"]*)"/);
  if (value) e.value = value[1];
  e.checked = /\bchecked\b/.test(match[2]);
  e.disabled = /\bdisabled\b/.test(match[2]);
  elements.set(match[3], e);
}
for (const match of html.matchAll(/<select\b[^>]*id="([^"]+)"[^>]*>([\s\S]*?)<\/select>/g)) {
  const select = elements.get(match[1]);
  for (const option of match[2].matchAll(/<option\b([^>]*)>([\s\S]*?)<\/option>/g)) {
    const e = new Element('option');
    e.value = option[1].match(/value="([^"]*)"/)?.[1] || '';
    select.appendChild(e);
    if (/selected/.test(option[1])) select.value = e.value;
  }
}
let client, wall = 1800000000000;
const blobs = new Map();
const context = {
  PlanKOImu: protocol, console,
  URLSearchParams,
  location: {search: '', hostname: '127.0.0.1', protocol: 'http:', origin: 'http://127.0.0.1'},
  navigator: {onLine: true}, TextEncoder,
  document: {getElementById: id => {
    assert.ok(elements.has(id), `Unknown DOM id: ${id}`); return elements.get(id);
  }, createElement: tag => new Element(tag), querySelectorAll: () => [], body: new Element('body')},
  Option: function(text, value) { const e = new Element('option'); e.value = value; e.textContent = text; return e; },
  Date: class extends Date { static now() { return wall; } },
  performance: {now: () => wall}, setInterval() {}, setTimeout() {}, clearTimeout() {},
  requestAnimationFrame() {}, addEventListener() {}, devicePixelRatio: 1,
  Blob, URL: {createObjectURL(blob) { const url = `blob:test-${blobs.size}`; blobs.set(url, blob); return url; }, revokeObjectURL() {}},
  alert: message => { throw new Error(`Unexpected alert: ${message}`); },
  Paho: {MQTT: {Client: class {
    constructor(host, port, route) { assert.equal(host, '127.0.0.1'); assert.equal(port, 9001); assert.equal(route, '/mqtt'); client = this; }
    isConnected() { return true; }
    connect(options) { assert.equal(options.mqttVersion, 4); options.onSuccess(); }
    subscribe(topic, options) { assert.equal(topic, 'planko/imu'); options.onSuccess(); }
    disconnect() {}
  }}}
};
context.window = context;
vm.createContext(context);
for (const script of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/g)) {
  if (script[1].trim()) vm.runInContext(script[1], context);
}
function send(seq, gateway = 'gw-test') {
  const data = {type: 'imu_stream', units: {accel: 'm/s^2', gyro: 'rad/s'},
    gateway: {clientId: gateway, bootId: 1, mqttSeq: seq},
    nodes: [1, 2].map(id => ({nodeId: id, sensor_uid: `uid-${id}`, mount_site: 'wrist',
      body_part: 'wrist', side: 'unknown', seq, ts: id * 10000 + seq * 50, boot_id: id,
      accel: [0, 0, 9.80665], gyro: [0.01, 0.02, 0.03], valid: true, pitch: 1, roll: 2, yaw: 3}))};
  client.onMessageArrived({destinationName: 'planko/imu', retained: false, payloadString: JSON.stringify(data)});
}
send(0);
assert.equal(elements.get('gatewayFilter').value, 'gw-test');
assert.equal(elements.get('recNodeList').children.length, 2);
elements.get('sharedActionLabel').value = 'case_flexion';
elements.get('guidedTimerCheck').checked = false;
elements.get('recStartBtn').click();
assert.equal(elements.get('gatewayFilter').disabled, true);
// Rendering is paused and a different (offline) node is selected; capture must continue for BOTH live nodes.
elements.get('pauseBtn').click();
elements.get('nodeSelect').value = '4';
elements.get('nodeSelect').fire('change');
for (let i = 1; i <= 40; i++) { wall += 50; send(i); }
send(41, 'another-gateway');
assert.equal(vm.runInContext('[...datasetRecorder.streams.values()].every(s => s.rows.length === 40)', context), true);
elements.get('recStopBtn').click();
assert.equal(elements.get('recDownloadList').children.length, 3);
assert.equal(elements.get('gatewayFilter').disabled, false);
assert.equal(elements.get('recDownloadAllBtn').disabled, false);
assert.equal(vm.runInContext('datasetRecorder.manifest().streams.length', context), 2);
assert.equal(vm.runInContext('[...datasetRecorder.streams.values()].every(s => s.rows.every(r => r.action_label === "case_flexion"))', context), true);
assert.equal(vm.runInContext('[...captureNodes.values()].every(e => e.status.textContent.includes("20.0 Hz"))', context), true);
console.log('PASS: actual index.html MQTT handlers, gateway pinning, one shared case label, simultaneous capture while display paused/offline, and two CSVs plus manifest');
