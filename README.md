# PlanKO Gateway and IMU Nodes

PlatformIO project for an ESP32-C3 Gateway and up to four ESP32-C3 IMU Nodes.
The Gateway receives sensor samples over BLE, keeps the MQTT stream for the
existing app, and serves a local dataset recorder.

## Firmware roles

Open `Code/` with PlatformIO and select the matching environment:

- `gateway`: BLE Central, Wi-Fi Station, MQTT publisher, pad inputs, and the
  dataset recorder at `http://192.168.1.4/`. It publishes dashboard telemetry
  through Mosquitto and receives targeted pad commands over MQTT.
- `imu_node_1` through `imu_node_4`: BLE Peripheral with a real MPU-6050 sensor.
  Connect SDA to GPIO 8 and SCL to GPIO 9. A missing sensor is reported as
  unavailable; the Node does not generate mock measurements.

The Gateway can connect to four Nodes at once. The recorder shows each Node's
connection and sensor identity. Configure its display name and body location in
the page; the defaults are Node 1 at the neck and Node 2 at the back/lumbar.

Open the root `index.html` dashboard in a browser. It connects to Mosquitto at
`wss://test.mosquitto.org:8081/mqtt`, subscribes to `planko/imu` and
`planko/pad`, and filters the public topics by Gateway ID. Pad buttons publish
targeted commands on the Gateway's `commandTopic`; the resulting pad event is
published back on `planko/pad`. These are simulated pad events and do not
electrically activate a GPIO input.

## Dataset capture

1. Connect the Gateway to the Wi-Fi network that uses the `192.168.1.x`
   subnet, then open `http://192.168.1.4/` on a device on that network.
2. Connect one or more Nodes and set the subject, session, trial, action, and
   each Node's body location.
3. Start capture. The page records real raw accelerometer and gyroscope samples
   from all connected Nodes into separate in-browser datasets. The guided phase
   sequence can be edited; manual phase buttons are available when guided mode
   is off.
4. Stop capture, review the phase boundaries and review checkboxes, then confirm
   to create one CSV download link per Node.

Capture data remains in the browser page's memory until the CSV files are
downloaded. The Gateway holds 256 samples per Node (about 12.8 seconds at 20 Hz)
in a rolling buffer for the page.

## MQTT

The data path remains **Node N -> BLE -> Gateway -> Mosquitto -> Subscriber**.
Only the Gateway connects to MQTT; Nodes retain their BLE peripheral role.
Gateway uses public `test.mosquitto.org:1883`; browser subscribers use
`wss://test.mosquitto.org:8081/mqtt`. Unique client IDs prevent clients from
disconnecting one another. This is a public test service, not a local broker.

The consolidated `planko/imu` stream targets 20 Hz (50 ms), with every packet
containing all connected nodes. Schema `planko-imu-v2` retains the existing
angles/accel and adds gyro in rad/s, explicit SI units, sensor identity, node
boot epoch, mount site/body part/side, validity and gateway MQTT sequence.
Accelerometer and gyro channels come from one paired BLE sample in the
Gateway history; no sensor values are fabricated. Pressure and biometric
topics remain at 4 Hz; pad and status topics remain event based.

### MQTT subscriber and model

```powershell
cd Code
python -m pip install -r requirements-ml.txt
python tools/serve_model.py --port 8765
```

Open **http://127.0.0.1:8765/monitor** (or `Code/WEB/index.html`) for the MQTT
subscriber. The top-level hardware remote also uses the Mosquitto WSS endpoint
described above.
Choose the exact Node ID and matching mount site/body part/side. Filter Gateway
ID to isolate your device on the public topic. Configure the actual mounting
context through the existing Gateway recorder at `http://192.168.1.4/`;
changing only the subscriber selection does not reconfigure a Node.

The monitor feeds **received MQTT samples** to `/api/predict-window` on the
local inference server; it no longer fetches sensor data from `/api/live`.
This local POST is computation after subscription, not an alternate sensor
transport. Recording and inference share the same six-axis SI sample adapter;
Python training and inference both call `imu_features.window_features`.
Missing gyro, invalid samples, gaps, reboots and context changes do not become
zero-filled training samples or windows that cross discontinuities. Display
pause keeps MQTT reception and sample accounting running.

The current default model contains **40 features**, a 1-second window at 20 Hz,
comprising 30 per-axis statistics, 6 magnitude statistics, 3 site/body/side
context flags and 1 relative-orientation validity flag,
and only the `wrist/wrist/unknown` context with `wrist_down`, `wrist_neutral`,
`wrist_up`. It has no trained phase classifier, so phase may remain `unknown`.
Neck/back require a separately trained compatible model; software alignment
does not make this wrist model support those sites. The model is unchanged,
and its evaluation does not establish independent held-out accuracy.

Tests (from project root):

```powershell
node --preserve-symlinks --preserve-symlinks-main Code/test/test_imu_protocol.cjs
python -B Code/test/test_model_contract.py -v
```

Existing HTTP recorder and legacy model testbench remain available separately.
See `Code/SUBSCRIBER_PROMPT.md` for the integration contract.

## Hardware and setup

- ESP32-C3 DevKitM-1 / SuperMini for the Gateway and each Node
- MPU-6050 on each Node (I2C address `0x68`, SDA GPIO 8, SCL GPIO 9)
- VS Code with PlatformIO
- Wi-Fi access point on the `192.168.1.x` subnet; reserve `192.168.1.4` for the
  Gateway to avoid an address conflict

Wi-Fi credentials and MQTT broker settings are configured in
`Code/src/main.cpp`. Build and upload each board using its PlatformIO role.
Gateway upload/monitor is **COM5**; Node 1 upload/monitor is **COM8**. Environments
`gateway`, `imu_node_1` through `imu_node_4` and their role flags are retained.
