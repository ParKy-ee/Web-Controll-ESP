# PlanKO Gateway and IMU Nodes

PlatformIO project for an ESP32-C3 Gateway and up to four ESP32-C3 IMU Nodes.
The Gateway receives sensor samples over BLE, keeps the MQTT stream for the
existing app, and serves a local dataset recorder.

## Firmware roles

Open `Code/` with PlatformIO and select the matching environment:

- `gateway`: BLE Central, Wi-Fi Station, MQTT publisher, pad inputs, and the
  dataset recorder at `http://192.168.1.4/`.
- `imu_node_1` through `imu_node_4`: BLE Peripheral with a real MPU-6050 sensor.
  Connect SDA to GPIO 8 and SCL to GPIO 9. A missing sensor is reported as
  unavailable; the Node does not generate mock measurements.

The Gateway can connect to four Nodes at once. The recorder shows each Node's
connection and sensor identity. Configure its display name and body location in
the page; the defaults are Node 1 at the neck and Node 2 at the back/lumbar.

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

The Gateway continues to publish the existing consolidated stream to
`planko/imu`, pad presses to `planko/pad`, and online status to `planko/status`.
The additional raw IMU BLE notification is consumed by the local recorder; it
does not change the MQTT stream schema.

## Hardware and setup

- ESP32-C3 DevKitM-1 / SuperMini for the Gateway and each Node
- MPU-6050 on each Node (I2C address `0x68`, SDA GPIO 8, SCL GPIO 9)
- VS Code with PlatformIO
- Wi-Fi access point on the `192.168.1.x` subnet; reserve `192.168.1.4` for the
  Gateway to avoid an address conflict

Wi-Fi credentials and MQTT broker settings are configured in
`Code/src/main.cpp`. Build and upload each board using its PlatformIO role.
