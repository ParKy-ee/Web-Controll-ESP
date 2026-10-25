# Multi-node IMU capture at 20 Hz

Firmware is `src/main.cpp`. Each IMU Node samples at 20 Hz and sends to the
Gateway over BLE. The Gateway publishes one consolidated `planko/imu` message
every 50 ms, containing all connected Nodes (up to four), to
`test.mosquitto.org:1883`. Adding Nodes does not create an MQTT connection or
publish loop per Node. The browser subscribes with MQTT over
`wss://test.mosquitto.org:8081/mqtt`.

## Record

1. Use the firmware environments in `platformio.ini`: `gateway` and a distinct
   `imu_node_1` through `imu_node_4` per sensor. Connect the Gateway to Wi-Fi.
   Gateway uses DHCP for its IP, route and DNS; there is no fixed local address.
   The optional HTTP recorder's current address is printed in Serial Monitor.
2. Open `WEB/index.html`, or start `python tools/serve_model.py --port 8765` and
   open `http://127.0.0.1:8765/monitor`. The project-root `index.html` still opens
   the separate hardware remote; use the monitor for this MQTT dataset recorder.
3. The monitor pins the first Gateway ID it receives. Set your exact Gateway ID
   in the filter if another Gateway is selected. Configure actual sensor
   mounting locations on the Gateway's HTTP recorder before capture.
4. Select the upper and lower Node, then choose one shared Case/Action Label.
   The exact same `action_label` is written to every selected Node sample, so
   the label describes one case rather than a body-part-specific action. Fill
   Subject, Session and Trial IDs.
5. Start recording both selected Nodes together. The capture makes one CSV per
   sensor plus a manifest; samples from other Nodes are ignored. Node cards
   report `LIVE` or briefly `HOLD`, accepted sample rate, count and gaps.
6. Stop recording. Download each per-node CSV and the capture manifest, or use
   Download All (the browser may ask to permit multiple downloads). Download
   before closing or refreshing the page; capture is held in browser memory.

Locations and sensor UIDs come from the firmware, not the display selectors.
Legacy firmware `body_part=back` at `mount_site=lumbar` is exported as training
`body_part=lumbar`; the original value is in `firmware_body_part`. A
sensor/context change produces a separate CSV. Reboots remain visible as epoch
changes inside a stream. The shared `case_*` labels in `pose_labels.json` work
across Node body parts and are accepted by the training and inference pipeline.

## Training contract

CSV includes raw six-axis SI channels (acceleration in m/s², gyro in rad/s),
orientation, node and gateway identities, independent device clocks, shared
host arrival time, MQTT sequence, sensor sequence, boot epochs, subject,
session, trial, action and phase metadata. Missing/invalid axes and mock
samples are excluded rather than filled with zeros. Duplicate and out-of-order
samples are excluded independently per sensor. Missing sequence numbers,
reboots, invalid-sample recovery and time gaps are marked with `gap_before`.
Training reads this marker and resets windows at discontinuities.

Guided phase timers produce **unreviewed cues**. Their phase labels are excluded
from phase training until reviewed and annotated. Manual phase annotations
are marked reviewed only when explicitly confirmed. Action labels must still
be checked against what the participant actually did.

Save the CSVs in `imu_output/`, then from `Code/`:

```powershell
$trainingCsvPaths = @(Get-ChildItem imu_output -Filter *.csv | Select-Object -ExpandProperty FullName)
python tools/train_model.py @trainingCsvPaths --window 1.0 --output models/new_model
```

The current training tool creates per-sensor windows from all supplied Node
CSVs and trains one action classifier with the shared case labels. It does not
join simultaneous Node samples into a single multi-sensor feature vector;
independent device clocks are not synchronized by publishing Nodes in the same
MQTT message. Keep whole trials/sessions/subjects together for held-out
evaluation. Existing model bundles are not retrained automatically by
recording.

The existing stream sends the latest available sample for each Node. Its
20 Hz publish target and QoS 0 do not guarantee that every original IMU sample
arrives. Observed missing samples are reported in the manifest, never
interpolated. Switching to Mosquitto does not remove MQTT or guarantee unlimited
capacity; use the recorded rates and gap counters to assess an actual capture.

## Automated validation

```powershell
node --preserve-symlinks --preserve-symlinks-main test/test_imu_protocol.cjs
node --preserve-symlinks --preserve-symlinks-main test/test_multinode_recorder.cjs
node --preserve-symlinks --preserve-symlinks-main test/test_multinode_page.cjs
python -B test/test_multinode_training.py -v
python -B test/test_model_contract.py -v
```

These use local synthetic samples and an in-process MQTT client double, including
two independent 20 Hz IMUs. They validate capture, page handlers, CSV parsing and
training windows. They do not establish live broker throughput, BLE reliability
or board firmware build/upload success.
