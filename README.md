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

Open the root `index.html` dashboard in a browser on the broker PC. It connects
to the local WebSocket listener at `ws://127.0.0.1:9001/mqtt`, subscribes to
`planko/imu` and `planko/pad`, and filters messages by Gateway ID. Pad buttons
publish targeted commands on the Gateway's `commandTopic`; the resulting pad
event is published back on `planko/pad`. These are simulated pad events and do
not electrically activate a GPIO input.

## Dataset capture

### Node battery telemetry (ESP32-C3 Super Mini, Nodes 1–4)

All four Node environments use GPIO4 (`BATTERY_ADC_PIN`, overrideable by a
build flag) for a 1S Li-ion/LiPo battery voltage divider:

```text
TP4056 OUT+ ---- 10k ohm ----+---- GPIO4
                           |
                        10k ohm
                           |
TP4056 OUT- ----------------+---- Node GND
```

An optional 100 nF ceramic capacitor goes between GPIO4 and OUT-/GND.
The existing OUT+ to Node 5V power wire is a separate branch; never connect
OUT+ directly to GPIO4. This measurement circuit does not regulate the Node's
power supply. Battery operation through the board's 5V input depends on its
onboard regulator and may stop before the cell is empty.

Nodes average 16 calibrated ADC millivolt readings, multiply by two, and send
a separate 5-byte battery BLE notification every 5 seconds and on subscription.
Battery reporting does not require a detected IMU. The percentage is a coarse
linear estimate (3.3V = 0%, 4.2V = 100%), not measured remaining capacity;
charging and load affect it. Readings outside 2.5–4.5V are marked unknown.

Flash the updated Gateway as well as each Node. The Gateway shows the battery
on its local Node cards and Serial Monitor, and includes `battery_valid`,
`battery_percent`, and `battery_mv` in `/api/nodes` and MQTT `planko/imu`.
Values become null when disconnected, invalid, or older than 15 seconds.
Existing IMU packet formats and sample sequence counters are unchanged.
The root `index.html` redirects to `remote.html`; that dashboard shows a
battery label only for connected Nodes with valid battery data. It hides the
label on Node disconnect, missing/expired data, Gateway change, or stream timeout.

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

The data path is **Node N -> BLE -> Gateway -> local Mosquitto -> Subscriber**.
Only the Gateway connects over MQTT; Nodes retain their BLE peripheral role.
Start the local Docker broker with `Code/tools/start_mosquitto.ps1`. It listens
on the PC's LAN IPv4 address at port 1883 for the Gateway and port 9001 with
WebSockets for the dashboard. The PC address is DHCP assigned: update
`MQTT_BROKER` in `Code/src/main.cpp` to the Wi-Fi address printed by the script,
then rebuild and upload the Gateway if that address changes. When opened from
`file://` on the broker PC, the root dashboard defaults to
`ws://127.0.0.1:9001/mqtt`; a dashboard on another device can use
`?broker=<PC-LAN-IP>`. Unique client IDs let the Gateway and dashboard connect
at the same time.

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
