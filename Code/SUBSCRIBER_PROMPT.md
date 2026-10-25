# Prompt: PlanKO subscriber integration

Keep the sensor transport exactly Node N -> BLE -> Gateway -> Mosquitto -> subscriber. Do not add Wi-Fi/MQTT to Nodes, replace BLE, or fetch sensor data over HTTP for MQTT inference. Preserve PlatformIO role flags; gateway is COM5 and imu_node_1 is COM8.

Use public test.mosquitto.org. Gateway/native clients use MQTT TCP 1883; browser uses wss://test.mosquitto.org:8081/mqtt. MQTT 3.1.1, QoS 0, unique subscriber client ID, clean session, keepalive 30 seconds. Subscribe to planko/imu on every successful connection and retry after disconnect using a single timer. Never fall back to EMQX.

The planko-imu-v2 JSON has type=imu_stream, units={accel:"m/s^2",gyro:"rad/s",orientation:"degrees"}, gateway={clientId,bootId,mqttSeq,uptimeMs,connectedNodes} and nodes=[{nodeId,sensor_uid,mount_site,body_part,side,boot_id,seq,ts,pitch,roll,yaw,accel:[x,y,z],gyro:[x,y,z],valid,calibrated,relative_orientation_valid,isMock,dropped}]. Nodes may be empty. ts is sensor milliseconds; gateway.mqttSeq is the MQTT sequence, while nodes[].seq is the BLE sample sequence. Do not mix these counters or gateways.

Use WEB/imu_protocol.js sampleFromMessage and SampleWindow for recording/inference. Require finite six-axis SI values; do not substitute zero for missing gyro. Match node identity, actual mounting context, side and boot epoch. Ignore duplicate samples and reset windows on missing sequence, time gaps, reboot or context changes. Firmware IMU bias calibration is not a relative-orientation calibration profile.

Read /api/health from the local model server for feature schema, feature columns, units, window_seconds, sampling_hz, gap_reset_seconds and supported contexts. POST received raw MQTT rows to /api/predict-window. Compute features only through tools/imu_features.py window_features, shared by training and inference. This POST is local prediction computation after MQTT subscription; sensor data must never be replaced by /api/live in this flow.

Current model is unchanged: 40 features, 1-second window, 20 Hz, wrist/wrist/unknown only, wrist_down/wrist_neutral/wrist_up, raw six axes, no phase model. Show unknown and a reason for unsupported sites; do not claim neck/back support. Show RX rate separately from render rate. Receive count/observed sequence gaps cannot establish complete send/receive equality without matching sender counters/manifest.

Keep the existing hardware remote and HTTP dataset recorder available, and retain existing upload environments. Validate with Code/test/test_imu_protocol.cjs and Code/test/test_model_contract.py. Report actual build/hardware test results separately from mocked or synthetic checks.
