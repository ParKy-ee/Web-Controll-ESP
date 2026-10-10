/**
 * ==============================================================================
 * Project: PlanKO - Smart Plank Training System
 * Hardware: ESP32-C3 (DevKitM-1 / SuperMini)
 *
 * Supports 2 Unified Roles via PlatformIO build flags:
 *   1. ROLE_GATEWAY:
 *      - BLE Central (connects to up to 4 IMU Nodes via BLE GATT)
 *      - Wi-Fi Station + WebSocket Server on Port 81
 *      - 16-pad Smart Mat Switch Inputs + Debounce + Serial Simulator
 *      - (Optional) MAX30102 Pulse Oximeter
 *      - Broadcasts consolidated IMU + Touch events to Expo App
 *
 *   2. ROLE_NODE:
 *      - BLE Peripheral (Advertises as PlanKO_IMU_1 .. PlanKO_IMU_4)
 *      - Reads MPU-6050 6-DOF IMU over I2C (SDA=4, SCL=5)
 *      - Computes Pitch, Roll, Yaw via Complementary Filter
 *      - Transmits 20-byte binary ImuPacket via BLE Notifications
 * ==============================================================================
 */

#include "common_types.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>

#if defined(ROLE_NODE)
// ==============================================================================
// ------------------------------ ROLE: IMU NODE
// --------------------------------
// ==============================================================================
#include "mpu6050_reader.h"
#include <NimBLEDevice.h>

#ifndef NODE_ID
#define NODE_ID 1
#endif

#ifndef I2C_SDA_PIN
#define I2C_SDA_PIN 4
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 5
#endif

// Sampling frequency: 20Hz (50ms) - Optimal for BLE & Wi-Fi coexistence and queue stability
const unsigned long SAMPLE_INTERVAL_MS = 50;

Mpu6050Reader imu;
NimBLEServer *pServer = nullptr;
NimBLECharacteristic *pImuCharacteristic = nullptr;
bool bleConnected = false;
uint16_t packetSequence = 0;
unsigned long lastSampleTime = 0;
unsigned long lastSerialPrintTime = 0;

#define NODE_LOG(x) do { if (Serial && Serial.availableForWrite() > 32) Serial.print(x); } while(0)
#define NODE_LOGLN(x) do { if (Serial && Serial.availableForWrite() > 32) Serial.println(x); } while(0)
#define NODE_LOGF(...) do { if (Serial && Serial.availableForWrite() > 32) Serial.printf(__VA_ARGS__); } while(0)

class ServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer *pServer) override {
    bleConnected = true;
    NODE_LOGLN("🔗 [BLE] Gateway Connected!");
  }
  void onConnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
    bleConnected = true;
    NODE_LOGF("🔗 [BLE] Gateway Connected! conn=%u\n", desc->conn_handle);
  }
  void onDisconnect(NimBLEServer *pServer) override {
    bleConnected = false;
    NODE_LOGLN("🔌 [BLE] Gateway Disconnected! Restarting Adv...");
    NimBLEDevice::startAdvertising();
  }
  void onDisconnect(NimBLEServer *pServer, ble_gap_conn_desc *desc) override {
    bleConnected = false;
    NODE_LOGF("🔌 [BLE] Gateway Disconnected! conn=%u. Restarting Adv...\n", desc->conn_handle);
    NimBLEDevice::startAdvertising();
  }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *pChar, ble_gap_conn_desc *desc,
                   uint16_t subVal) override {
    NODE_LOGF("🔔 [NODE-BLE] Client %d subscription status changed: subVal = %u\n",
              desc->conn_handle, subVal);
  }
};

void printNodeBanner() {
  NODE_LOGLN("\n****************************************************************");
  NODE_LOGF("🤖 [PlanKO IMU Node #%d] Firmware Started!\n", NODE_ID);
  NODE_LOGF("📡 BLE Advertising Name: PlanKO_IMU_%d\n", NODE_ID);
  NODE_LOGF("🔌 I2C Pins: SDA = GPIO %d, SCL = GPIO %d\n", I2C_SDA_PIN, I2C_SCL_PIN);
  NODE_LOGLN("💡 Type 'CAL' in Serial Monitor to re-calibrate IMU sensor");
  NODE_LOGLN("****************************************************************\n");
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  delay(100);

  printNodeBanner();

  // 1. Initialize MPU-6050 IMU
  NODE_LOGLN("🔍 [IMU] Initializing MPU-6050...");
  if (!imu.begin(I2C_SDA_PIN, I2C_SCL_PIN)) {
    NODE_LOGLN("❌ [IMU] MPU-6050 not detected. Retrying in background...");
  }

  // 2. Initialize NimBLE Peripheral
  char bleName[20];
  snprintf(bleName, sizeof(bleName), "PlanKO_IMU_%d", NODE_ID);
  NimBLEDevice::init(bleName);
  NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum TX power

  pServer = NimBLEDevice::createServer();
  pServer->setCallbacks(new ServerCallbacks());

  NimBLEService *pService = pServer->createService(PLANKO_SERVICE_UUID);
  pImuCharacteristic = pService->createCharacteristic(
      PLANKO_CHARACTERISTIC_UUID,
      NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  pImuCharacteristic->setCallbacks(new CharacteristicCallbacks());
  pService->start();
  NODE_LOGF("🔍 [NODE-BLE] pImuChar handle: %d\n",
            pImuCharacteristic->getHandle());

  NimBLEAdvertising *pAdvertising = NimBLEDevice::getAdvertising();
  NimBLEAdvertisementData advData;
  advData.setFlags(0x06); // General Discovery, BR/EDR not supported
  advData.setName(bleName);
  pAdvertising->setAdvertisementData(advData);

  NimBLEAdvertisementData scanResponseData;
  scanResponseData.setCompleteServices(NimBLEUUID(PLANKO_SERVICE_UUID));
  pAdvertising->setScanResponseData(scanResponseData);

  pAdvertising->setMinInterval(32);
  pAdvertising->setMaxInterval(64);
  pAdvertising->start();

  NODE_LOGF("🚀 [BLE] Advertising as '%s' [%s] - Waiting for Gateway...\n",
            bleName, NimBLEDevice::getAddress().toString().c_str());
}

void loop() {
  unsigned long now = millis();

  // Check for Serial commands (e.g. CAL) non-blockingly
  if (Serial.available()) {
    static String rxCmd = "";
    while (Serial.available()) {
      char c = (char)Serial.read();
      if (c == '\n' || c == '\r') {
        rxCmd.trim();
        rxCmd.toUpperCase();
        if (rxCmd == "CAL") {
          imu.calibrate();
        }
        rxCmd = "";
      } else {
        rxCmd += c;
      }
    }
  }

  // Ensure advertising stays active if not connected
  static unsigned long lastAdvCheck = 0;
  if (!bleConnected && (now - lastAdvCheck >= 2000)) {
    lastAdvCheck = now;
    NimBLEAdvertising *adv = NimBLEDevice::getAdvertising();
    if (adv && !adv->isAdvertising()) {
      adv->start();
    }
  }

  // Read IMU and send BLE Notification
  if (now - lastSampleTime >= SAMPLE_INTERVAL_MS) {
    lastSampleTime = now;

    float curPitch = 0.0f;
    float curRoll = 0.0f;
    float curYaw = 0.0f;
    float curAx = 0.0f;
    float curAy = 0.0f;
    float curAz = 9.8f;
    uint8_t curStatus = 0;

    if (imu.isDetected) {
      imu.update();
      curPitch = imu.pitch;
      curRoll = imu.roll;
      curYaw = imu.yaw;
      curAx = imu.ax;
      curAy = imu.ay;
      curAz = imu.az;
      curStatus = imu.isCalibrated ? 1 : 0;
    } else {
      // [MOCK SIMULATION] ระบุชัดเจนว่าเป็น Mock (จำลองการสั่นไหวและมุมเอียงของ Plank)
      imu.update(); // Keeps background auto-detect active
      float t = now / 1000.0f;
      curPitch = sinf(t * 1.2f + (NODE_ID * 0.5f)) * 3.5f;
      curRoll = cosf(t * 0.8f + (NODE_ID * 0.3f)) * 2.0f;
      curYaw = sinf(t * 0.2f) * 1.0f;
      curAx = sinf(t * 1.5f) * 0.1f;
      curAy = cosf(t * 1.5f) * 0.1f;
      curAz = 9.8f + sinf(t * 1.2f) * 0.05f;
      curStatus = 2; // 2 = MOCK DATA
    }

    if (pImuCharacteristic != nullptr && (bleConnected || pImuCharacteristic->getSubscribedCount() > 0)) {
      ImuPacket pkt;
      pkt.nodeId = NODE_ID;
      pkt.sequence = ++packetSequence;
      pkt.timestamp = now;
      pkt.pitch = (int16_t)(curPitch * 100.0f);
      pkt.roll = (int16_t)(curRoll * 100.0f);
      pkt.yaw = (int16_t)(curYaw * 100.0f);
      pkt.ax = (int16_t)(curAx * 100.0f);
      pkt.ay = (int16_t)(curAy * 100.0f);
      pkt.az = (int16_t)(curAz * 100.0f);
      pkt.status = curStatus;

      pImuCharacteristic->notify((uint8_t *)&pkt, sizeof(pkt));
    }
  }

  // Print telemetry to Serial once every 2 seconds
  if (now - lastSerialPrintTime >= 2000) {
    lastSerialPrintTime = now;
    if (Serial && Serial.availableForWrite() > 64) {
      float displayPitch =
          imu.isDetected ? imu.pitch
                         : (sinf(now / 1000.0f * 1.2f + (NODE_ID * 0.5f)) * 3.5f);
      float displayRoll =
          imu.isDetected ? imu.roll
                         : (cosf(now / 1000.0f * 0.8f + (NODE_ID * 0.3f)) * 2.0f);
      size_t subs = pImuCharacteristic ? pImuCharacteristic->getSubscribedCount() : 0;
      uint16_t hdl = pImuCharacteristic ? pImuCharacteristic->getHandle() : 0;
      Serial.printf(
          "📊 [Node #%d]%s Pitch: %5.1f° | Roll: %5.1f° | BLE: %s (Subs: %u, hdl: %u) | Seq: %u\n",
          NODE_ID, imu.isDetected ? " [REAL IMU]" : " [MOCK SIMULATOR]",
          displayPitch, displayRoll, bleConnected ? "CONNECTED" : "ADVERTISING",
          (unsigned int)subs, (unsigned int)hdl, packetSequence);
    }
  }
}

#elif defined(ROLE_GATEWAY)
// ==============================================================================
// ----------------------------- ROLE: GATEWAY
// ----------------------------------
// ==============================================================================
#include <NimBLEDevice.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <esp_wifi.h>

// Wi-Fi Configuration
const char *WIFI_SSID = "APISIT164 @2.4G";
const char *WIFI_PASS = "09E2898E";

// MQTT Configuration (broker.emqx.io)
const char *MQTT_BROKER = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;
const char *TOPIC_IMU = "planko/imu";
const char *TOPIC_PAD = "planko/pad";
const char *TOPIC_STATUS = "planko/status";
char mqttClientId[32] = "PlanKO_Gateway_ESP32";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Static IP Configuration
IPAddress STATIC_IP(192, 168, 1, 200);
IPAddress GATEWAY_IP(192, 168, 1, 1);
IPAddress SUBNET_MASK(255, 255, 255, 0);
IPAddress DNS_PRIMARY(192, 168, 1, 1);
IPAddress DNS_SECONDARY(8, 8, 8, 8);

// 16-Pad GPIO Hardware Configuration (ESP32-C3 Safe Pins)
struct PadConfig {
  const char *name;
  int8_t pin;
};

const PadConfig PAD_CONFIGS[] = {
    {"L1", 0},  {"L2", 1},  {"L3", 2},  {"L4", 3},  {"L5", 4},  {"L6", 5},
    {"L7", 6},  {"L8", 7},  {"R1", 10}, {"R2", -1}, {"R3", -1}, {"R4", -1},
    {"R5", -1}, {"R6", -1}, {"R7", -1}, {"R8", -1}};

const size_t TOTAL_PADS = sizeof(PAD_CONFIGS) / sizeof(PAD_CONFIGS[0]);

struct PadSensorState {
  const char *sensorId;
  int8_t pin;
  int lastRawState;
  int debouncedState;
  unsigned long lastDebounceTime;
};

PadSensorState sensors[TOTAL_PADS];
const unsigned long DEBOUNCE_DELAY_MS = 40;

// BLE Central State for 4 IMU Nodes
const int MAX_NODES = 4;
NodeTelemetry nodesTelemetry[MAX_NODES];
NimBLEClient *pNodeClients[MAX_NODES] = {nullptr, nullptr, nullptr, nullptr};
NimBLEAdvertisedDevice *pendingConnectDevices[MAX_NODES] = {nullptr, nullptr,
                                                            nullptr, nullptr};

unsigned long lastMqttStreamTime = 0;
const unsigned long MQTT_STREAM_INTERVAL_MS = 250; // 4Hz MQTT Stream (reliable, adheres to cloud broker limits)
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastBleScanTime = 0;
const unsigned long STALE_TIMEOUT_MS =
    15000; // Force disconnect if no data for 15s (prevents network jitter from dropping BLE)
bool isScanning = false;

// Forward declaration
void sendPadPressEvent(const char *sensorId);
void publishImuStream();
void maintainMqttConnection();
void resumeBleScanIfNeeded(bool forceRestart = false);

// BLE Client Callbacks
class GatewayClientCallbacks : public NimBLEClientCallbacks {
public:
  uint8_t nodeId;
  GatewayClientCallbacks(uint8_t id) : nodeId(id) {}

  void onConnect(NimBLEClient *pClient) override {
    Serial.printf("✅ [GATEWAY-BLE] Connected to IMU Node #%d [%s]\n",
                  nodeId, pClient->getPeerAddress().toString().c_str());
    nodesTelemetry[nodeId - 1].connected = true;
    nodesTelemetry[nodeId - 1].lastReceivedMillis = millis();
  }

  void onDisconnect(NimBLEClient *pClient) override {
    Serial.printf("❌ [GATEWAY-BLE] Disconnected from IMU Node #%d.\n", nodeId);
    nodesTelemetry[nodeId - 1].connected = false;
    nodesTelemetry[nodeId - 1].lastReceivedMillis = 0;
    nodesTelemetry[nodeId - 1].lastSequence = 0;

    if (pClient != nullptr) {
      NimBLEAddress peer = pClient->getPeerAddress();
      while (NimBLEDevice::isIgnored(peer)) {
        NimBLEDevice::removeIgnored(peer);
      }
    }

    if (pendingConnectDevices[nodeId - 1] != nullptr) {
      delete pendingConnectDevices[nodeId - 1];
      pendingConnectDevices[nodeId - 1] = nullptr;
    }

    // Force restart scanner with high duty cycle immediately
    resumeBleScanIfNeeded(true);
  }
};

// BLE Notification Callback for receiving ImuPacket from Nodes
void notifyCallback(NimBLERemoteCharacteristic *pRemoteChar, uint8_t *pData,
                    size_t length, bool isNotify) {
  if (length < sizeof(ImuPacket)) {
    Serial.printf("⚠️ [GW-NOTIFY-LEN] Short packet: %u bytes (expected %u)\n",
                  (unsigned int)length, (unsigned int)sizeof(ImuPacket));
    return;
  }

  const ImuPacket *pkt = (const ImuPacket *)pData;
  if (pkt->nodeId < 1 || pkt->nodeId > MAX_NODES)
    return;

  int idx = pkt->nodeId - 1;
  NodeTelemetry &telemetry = nodesTelemetry[idx];

  if (telemetry.lastSequence == 0) {
    Serial.printf("🎉 [GW-RECV] First packet from Node #%d received! Seq: %u\n", pkt->nodeId, pkt->sequence);
  }

  // Check packet drop sequence
  if (telemetry.lastSequence != 0 &&
      pkt->sequence > telemetry.lastSequence + 1) {
    uint16_t dropped = (pkt->sequence - telemetry.lastSequence - 1);
    telemetry.totalDropped += dropped;
    Serial.printf(
        "⚠️ [GATEWAY-BLE] Node #%d Dropped %u packets (Seq: %u -> %u)\n",
        pkt->nodeId, dropped, telemetry.lastSequence, pkt->sequence);
  }
  telemetry.lastSequence = pkt->sequence;
  telemetry.lastTimestamp = pkt->timestamp;
  telemetry.pitch = pkt->pitch / 100.0f;
  telemetry.roll = pkt->roll / 100.0f;
  telemetry.yaw = pkt->yaw / 100.0f;
  telemetry.ax = pkt->ax / 100.0f;
  telemetry.ay = pkt->ay / 100.0f;
  telemetry.az = pkt->az / 100.0f;
  telemetry.status = pkt->status;
  telemetry.lastReceivedMillis = millis();

  // Log live telemetry from node (rate-limited to 1s per node)
  static unsigned long lastNodeLogTime[MAX_NODES] = {0, 0, 0, 0};
  if (millis() - lastNodeLogTime[idx] >= 1000) {
    lastNodeLogTime[idx] = millis();
    Serial.printf("📥 [GW-RECV] Node #%d%s | Seq: %5u | Pitch: %5.1f° | Roll: "
                  "%5.1f° | Yaw: %5.1f° | Drops: %u\n",
                  pkt->nodeId, (pkt->status == 2) ? " [MOCK]" : " [REAL]",
                  pkt->sequence, telemetry.pitch, telemetry.roll, telemetry.yaw,
                  telemetry.totalDropped);
  }
}

// Advertised Device Callback
class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
    int id = 0;
    std::string name = advertisedDevice->getName();
    if (name.rfind("PlanKO_IMU_", 0) == 0 && name.length() >= 12) {
      id = name[11] - '0';
    } else if (advertisedDevice->isAdvertisingService(NimBLEUUID(PLANKO_SERVICE_UUID))) {
      // Recognized via 128-bit PlanKO Service UUID even if name is deferred to scan response
      id = 1;
    }

    if (id >= 1 && id <= MAX_NODES) {
      int idx = id - 1;
      // If node is already connected, ignore advertisement
      if (nodesTelemetry[idx].connected) {
        return;
      }

      // If connection already pending, ignore
      if (pendingConnectDevices[idx] != nullptr) {
        return;
      }

      Serial.printf("🔎 [GATEWAY-BLE] Found Node #%d ('%s') [%s] RSSI: %d\n", id,
                    name.c_str(), advertisedDevice->getAddress().toString().c_str(),
                    advertisedDevice->getRSSI());
      pendingConnectDevices[idx] =
          new NimBLEAdvertisedDevice(*advertisedDevice);
    }
  }
};

void resumeBleScanIfNeeded(bool forceRestart) {
  int connectedCount = 0;
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodesTelemetry[i].connected)
      connectedCount++;
  }

  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (!pScan)
    return;

  if (connectedCount < MAX_NODES) {
    uint16_t desiredWindow = (connectedCount == 0) ? 120 : 32;
    static unsigned long lastScanCycle = 0;
    static int lastConnectedCount = -1;
    unsigned long now = millis();

    // Restart scan if:
    // 1. Scanner is not currently running
    // 2. forceRestart requested (e.g. immediately on disconnect)
    // 3. connectedCount changed (need to adjust duty cycle between 75% and 20%)
    // 4. Scanner has been running for >= 3000ms (re-arms ESP32-C3 controller GAP state to prevent stalls)
    bool shouldRestart = forceRestart || 
                         !pScan->isScanning() || 
                         (connectedCount != lastConnectedCount) || 
                         (now - lastScanCycle >= 3000);

    if (shouldRestart) {
      lastScanCycle = now;
      lastConnectedCount = connectedCount;

      if (pScan->isScanning()) {
        pScan->stop();
      }
      pScan->clearResults();
      pScan->setInterval(160);         // 100ms
      pScan->setWindow(desiredWindow); // 75ms (no nodes) or 20ms (coexistence)
      pScan->setActiveScan(false);     // Passive scan: immediate ADV_IND reception without RF collision
      pScan->start(0, nullptr, false);
    }
  } else {
    // All 4 nodes connected: stop scanning to give 100% radio bandwidth to data & Wi-Fi
    if (pScan->isScanning()) {
      pScan->stop();
      pScan->clearResults();
    }
  }
}

void connectToNode(int idx) {
  int id = idx + 1;
  NimBLEAdvertisedDevice *advDevice = pendingConnectDevices[idx];
  if (advDevice == nullptr)
    return;

  Serial.printf("⏳ [GATEWAY-BLE] Connecting to Node #%d [%s]...\n",
                id, advDevice->getAddress().toString().c_str());

  // Stop scanning before connecting to avoid radio contention
  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (pScan && pScan->isScanning()) {
    pScan->stop();
  }

  NimBLEAddress peerAddr = advDevice->getAddress();

  NimBLEClient *pClient = nullptr;
  if (NimBLEDevice::getClientListSize() > 0) {
    pClient = NimBLEDevice::getClientByPeerAddress(peerAddr);
  }

  if (pClient != nullptr) {
    if (pClient->isConnected()) {
      pClient->disconnect();
      delay(50);
    }
  } else {
    pClient = NimBLEDevice::getDisconnectedClient();
    if (pClient == nullptr) {
      pClient = NimBLEDevice::createClient();
      if (pClient == nullptr) {
        Serial.printf("❌ [GATEWAY-BLE] Could not create client for Node #%d\n", id);
        while (NimBLEDevice::isIgnored(peerAddr)) {
          NimBLEDevice::removeIgnored(peerAddr);
        }
        delete advDevice;
        pendingConnectDevices[idx] = nullptr;
        resumeBleScanIfNeeded(true);
        return;
      }
    }
  }

  pNodeClients[idx] = pClient;
  pClient->setClientCallbacks(new GatewayClientCallbacks(id), true);
  // Connection parameters: 20-40ms interval, latency 2, supervision timeout 200 (2.0s)
  pClient->setConnectionParams(16, 32, 2, 200);
  pClient->setConnectTimeout(5);

  bool connected = false;
  if (pClient->isConnected()) {
    connected = true;
  } else {
    connected = pClient->connect(advDevice, false);
  }

  if (!connected) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to connect to Node #%d (will retry)\n", id);
    while (NimBLEDevice::isIgnored(peerAddr)) {
      NimBLEDevice::removeIgnored(peerAddr);
    }
    delete advDevice;
    pendingConnectDevices[idx] = nullptr;
    resumeBleScanIfNeeded(true);
    return;
  }

  NimBLERemoteService *pRemoteService =
      pClient->getService(PLANKO_SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.printf("❌ [GATEWAY-BLE] Failed to find service on Node #%d\n", id);
    pClient->disconnect();
    while (NimBLEDevice::isIgnored(peerAddr)) {
      NimBLEDevice::removeIgnored(peerAddr);
    }
    delete advDevice;
    pendingConnectDevices[idx] = nullptr;
    resumeBleScanIfNeeded(true);
    return;
  }

  NimBLERemoteCharacteristic *pRemoteChar =
      pRemoteService->getCharacteristic(PLANKO_CHARACTERISTIC_UUID);
  if (pRemoteChar == nullptr || !pRemoteChar->canNotify()) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to find notify characteristic on Node #%d\n",
        id);
    pClient->disconnect();
    while (NimBLEDevice::isIgnored(peerAddr)) {
      NimBLEDevice::removeIgnored(peerAddr);
    }
    delete advDevice;
    pendingConnectDevices[idx] = nullptr;
    resumeBleScanIfNeeded(true);
    return;
  }

  // Check CCCD descriptor
  NimBLERemoteDescriptor *pDesc = pRemoteChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
  Serial.printf("🔍 [GATEWAY-BLE] Node #%d Char handle: %d, CCCD handle: %d, canNotify: %d\n",
                id, pRemoteChar->getHandle(), pDesc ? pDesc->getHandle() : 0, pRemoteChar->canNotify());

  // Subscribe with response = false (standard non-blocking write to CCCD)
  bool subOk = pRemoteChar->subscribe(true, notifyCallback, false);
  Serial.printf("🔍 [GATEWAY-BLE] Node #%d subscribe returned: %s\n",
                id, subOk ? "TRUE" : "FALSE");

  if (!subOk && nodesTelemetry[idx].lastReceivedMillis == 0) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to subscribe to Node #%d notifications\n", id);
    pClient->disconnect();
    while (NimBLEDevice::isIgnored(peerAddr)) {
      NimBLEDevice::removeIgnored(peerAddr);
    }
    delete advDevice;
    pendingConnectDevices[idx] = nullptr;
    resumeBleScanIfNeeded(true);
    return;
  }

  Serial.printf(
      "🎉 [GATEWAY-BLE] Successfully subscribed to Node #%d telemetry!\n", id);
  nodesTelemetry[idx].connected = true;
  nodesTelemetry[idx].lastReceivedMillis = millis();
  nodesTelemetry[idx].lastSequence = 0;
  delete advDevice;
  pendingConnectDevices[idx] = nullptr;

  resumeBleScanIfNeeded();
}

void maintainMqttConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      Serial.print("📡 [MQTT] Connecting to broker.emqx.io:1883... ");
      // Connect with Last Will and Testament on planko/status (non-retained)
      if (mqttClient.connect(mqttClientId, TOPIC_STATUS, 0, false, "offline")) {
        Serial.println("CONNECTED! 🎉");
        mqttClient.publish(TOPIC_STATUS, "online", false);
        Serial.printf("📡 [MQTT] Ready! Publishing to '%s' and '%s'\n",
                      TOPIC_IMU, TOPIC_PAD);
      } else {
        Serial.printf("failed, rc=%d (will retry in 5s)\n", mqttClient.state());
      }
    }
  }
}

void sendPadPressEvent(const char *sensorId) {
  char jsonBuffer[64];
  snprintf(jsonBuffer, sizeof(jsonBuffer), "{\"sensorId\":\"%s\",\"value\":1}",
           sensorId);
  if (mqttClient.connected()) {
    mqttClient.publish(TOPIC_PAD, jsonBuffer);
  }
  Serial.printf("⚡ [PAD EVENT] %s (Published to %s)\n", sensorId, TOPIC_PAD);
}

void publishImuStream() {
  if (!mqttClient.connected())
    return;

  // Check how many nodes are connected
  int connectedCount = 0;
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodesTelemetry[i].connected)
      connectedCount++;
  }

  unsigned long now = millis();
  // If no nodes are connected, throttle stream to 1Hz (every 1000ms) to prevent broker flooding
  static unsigned long lastIdleBroadcastTime = 0;
  if (connectedCount == 0) {
    if (now - lastIdleBroadcastTime < 1000) {
      return;
    }
    lastIdleBroadcastTime = now;
  }

  JsonDocument doc;
  doc["type"] = "imu_stream";

  JsonObject gw = doc["gateway"].to<JsonObject>();
  gw["uptimeMs"] = millis();
  gw["connectedNodes"] = connectedCount;

  JsonArray nodes = doc["nodes"].to<JsonArray>();
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodesTelemetry[i].connected &&
        (nodesTelemetry[i].lastReceivedMillis == 0 ||
         (now - nodesTelemetry[i].lastReceivedMillis) < STALE_TIMEOUT_MS)) {
      JsonObject n = nodes.add<JsonObject>();
      n["nodeId"] = i + 1;
      n["seq"] = nodesTelemetry[i].lastSequence;
      n["ts"] = nodesTelemetry[i].lastTimestamp;
      n["pitch"] = round(nodesTelemetry[i].pitch * 10.0f) / 10.0f;
      n["roll"] = round(nodesTelemetry[i].roll * 10.0f) / 10.0f;
      n["yaw"] = round(nodesTelemetry[i].yaw * 10.0f) / 10.0f;
      n["dropped"] = nodesTelemetry[i].totalDropped;
      n["isMock"] = (nodesTelemetry[i].status == 2);

      JsonArray acc = n["accel"].to<JsonArray>();
      acc.add(round(nodesTelemetry[i].ax * 100.0f) / 100.0f);
      acc.add(round(nodesTelemetry[i].ay * 100.0f) / 100.0f);
      acc.add(round(nodesTelemetry[i].az * 100.0f) / 100.0f);
    }
  }

  String output;
  serializeJson(doc, output);
  bool pubOk = mqttClient.publish(TOPIC_IMU, output.c_str());
  static unsigned long lastPubFailLog = 0;
  if (!pubOk && (millis() - lastPubFailLog > 3000)) {
    lastPubFailLog = millis();
    Serial.printf("⚠️ [MQTT] Publish to '%s' failed! rc=%d\n", TOPIC_IMU, mqttClient.state());
  }
}

void handlePadInputs() {
  unsigned long now = millis();
  for (size_t i = 0; i < TOTAL_PADS; i++) {
    if (sensors[i].pin < 0)
      continue;

    int rawReading = digitalRead(sensors[i].pin);
    if (rawReading != sensors[i].lastRawState) {
      sensors[i].lastDebounceTime = now;
    }

    if ((now - sensors[i].lastDebounceTime) > DEBOUNCE_DELAY_MS) {
      if (rawReading != sensors[i].debouncedState) {
        sensors[i].debouncedState = rawReading;
        if (sensors[i].debouncedState == LOW) {
          sendPadPressEvent(sensors[i].sensorId);
        }
      }
    }
    sensors[i].lastRawState = rawReading;
  }
}

void handleSerialSimulator() {
  if (!Serial.available())
    return;
  String input = Serial.readStringUntil('\n');
  input.trim();
  input.toUpperCase();
  if (input.length() == 0)
    return;

  if (input == "HELP") {
    Serial.println("\n--- [PlanKO Gateway Simulator] ---");
    Serial.println("Type 'L1' - 'L8' or 'R1' - 'R8' to trigger pad event");
    Serial.println("Type 'STATUS' to see connected BLE nodes & stats");
    return;
  }

  if (input == "STATUS") {
    Serial.println("\n--- [Gateway Telemetry Status] ---");
    for (int i = 0; i < MAX_NODES; i++) {
      Serial.printf(
          "  Node #%d: %s | Seq: %u | Dropped: %u | P:%.1f R:%.1f Y:%.1f\n",
          i + 1, nodesTelemetry[i].connected ? "CONNECTED" : "OFFLINE",
          nodesTelemetry[i].lastSequence, nodesTelemetry[i].totalDropped,
          nodesTelemetry[i].pitch, nodesTelemetry[i].roll,
          nodesTelemetry[i].yaw);
    }
    Serial.printf("  MQTT: %s | Gateway IP: %s\n\n",
                  mqttClient.connected() ? "CONNECTED" : "DISCONNECTED",
                  WiFi.localIP().toString().c_str());
    return;
  }

  for (size_t i = 0; i < TOTAL_PADS; i++) {
    if (input == PAD_CONFIGS[i].name) {
      Serial.printf("🎮 [SIMULATOR] Triggered: %s\n", input.c_str());
      sendPadPressEvent(PAD_CONFIGS[i].name);
      return;
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);
  // Give USB CDC time to attach
  for (int i = 0; i < 20 && !Serial; i++) {
    delay(100);
  }
  delay(500);

  Serial.println(
      "\n****************************************************************");
  Serial.println("🌐 [PlanKO GATEWAY] Firmware Initializing...");
  Serial.println(
      "****************************************************************");

  // Initialize Pads
  for (size_t i = 0; i < TOTAL_PADS; i++) {
    sensors[i].sensorId = PAD_CONFIGS[i].name;
    sensors[i].pin = PAD_CONFIGS[i].pin;
    sensors[i].lastRawState = HIGH;
    sensors[i].debouncedState = HIGH;
    sensors[i].lastDebounceTime = 0;
    if (sensors[i].pin >= 0) {
      pinMode(sensors[i].pin, INPUT_PULLUP);
    }
  }

  // Connect to Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);
  WiFi.setAutoReconnect(true);

  WiFi.onEvent([](WiFiEvent_t event, WiFiEventInfo_t info) {
    switch (event) {
    case ARDUINO_EVENT_WIFI_STA_CONNECTED:
      Serial.println("\n📡 [WIFI] Associated with Wi-Fi AP!");
      break;
    case ARDUINO_EVENT_WIFI_STA_GOT_IP:
      Serial.printf("\n🎉 [WIFI] Connected! IP: %s\n",
                    WiFi.localIP().toString().c_str());
      break;
    case ARDUINO_EVENT_WIFI_STA_DISCONNECTED: {
      Serial.printf("\n⚠️ [WIFI] Disconnected (Reason: %d)\n",
                    info.wifi_sta_disconnected.reason);
      NimBLEScan *s = NimBLEDevice::getScan();
      if (s && s->isScanning()) {
        s->stop(); // Pause BLE scan so Wi-Fi can reconnect without radio interference
      }
      break;
    }
    default:
      break;
    }
  });

  WiFi.disconnect(true, true); // Clear any stale NVS credentials / Wi-Fi state
  delay(200);

  Serial.println("🔍 [WIFI] Scanning available Wi-Fi networks...");
  int n = WiFi.scanNetworks();
  Serial.printf("📡 [WIFI] Found %d networks:\n", n);
  int targetChannel = 0;
  for (int i = 0; i < n; ++i) {
    Serial.printf("   - '%s' (RSSI: %d dBm, Ch: %d, Auth: %d)\n",
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i), WiFi.encryptionType(i));
    if (WiFi.SSID(i) == WIFI_SSID) {
      targetChannel = WiFi.channel(i);
    }
  }
  WiFi.scanDelete();

  // Enable PMF (Protected Management Frames) for compatibility with modern Wi-Fi routers
  wifi_config_t current_conf;
  esp_wifi_get_config(WIFI_IF_STA, &current_conf);
  current_conf.sta.pmf_cfg.capable = true;
  current_conf.sta.pmf_cfg.required = false;
  esp_wifi_set_config(WIFI_IF_STA, &current_conf);

  WiFi.setTxPower(WIFI_POWER_8_5dBm); // Low TX power prevents voltage drop on ESP32-C3 DevKit during auth
  Serial.printf("⏳ [WIFI] Connecting to SSID: '%s'...\n", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    attempts++;
    if (attempts % 30 == 0) {
      Serial.printf("\n⏳ Still connecting to '%s' (Attempt %d). Retrying...\n",
                    WIFI_SSID, attempts);
      WiFi.disconnect();
      delay(200);
      if (targetChannel > 0) {
        WiFi.begin(WIFI_SSID, WIFI_PASS, targetChannel);
      } else {
        WiFi.begin(WIFI_SSID, WIFI_PASS);
      }
    }
  }
  Serial.println();

  Serial.printf("🎉 Wi-Fi Connected! IP: %s\n",
                WiFi.localIP().toString().c_str());

  // Initialize MQTT Client
  snprintf(mqttClientId, sizeof(mqttClientId), "PlanKO_GW_%08X",
           (uint32_t)ESP.getEfuseMac());
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(1024);
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(5);
  Serial.printf("🔌 MQTT Broker: %s:%u (ClientID: %s)\n", MQTT_BROKER,
                MQTT_PORT, mqttClientId);
  maintainMqttConnection();
  Serial.printf("💾 [MEMORY] Free Heap: %u bytes\n", ESP.getFreeHeap());

  Serial.println("👉 [INIT] Step 1: Enabling modem sleep for coexistence...");
  Serial.flush();
  WiFi.setSleep(true);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);

  Serial.println("👉 [INIT] Step 2: Initializing NimBLEDevice...");
  Serial.flush();
  NimBLEDevice::init("PlanKO_Gateway");

  Serial.println("👉 [INIT] Step 3: Configuring BLE Scan...");
  Serial.flush();
  NimBLEScan *pScan = NimBLEDevice::getScan();
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(), true); // wantDuplicates = true
  pScan->setActiveScan(false); // Passive scan: immediate ADV_IND reception without RF contention
  pScan->setDuplicateFilter(false);
  pScan->setMaxResults(0);
  pScan->setInterval(160); // 100ms interval
  pScan->setWindow(120);   // 75ms window (75% duty cycle)

  Serial.println("👉 [INIT] Step 4: Starting BLE Continuous Scan...");
  Serial.flush();
  pScan->start(0, nullptr, false);
  isScanning = true;
  Serial.println("🎉 [INIT] Setup completed! Scanner active, MQTT connected.\n");
  Serial.flush();
}

void loop() {
  maintainMqttConnection();
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  handlePadInputs();
  handleSerialSimulator();

  unsigned long now = millis();

  // Check for pending BLE connections (one at a time to keep MQTT & Wi-Fi responsive)
  for (int i = 0; i < MAX_NODES; i++) {
    if (pendingConnectDevices[i] != nullptr) {
      connectToNode(i);
      break;
    }
  }

  // Detect stale BLE nodes (no data received for 2.0s) and disconnect
  unsigned long freshNow = millis();
  for (int i = 0; i < MAX_NODES; i++) {
    if (nodesTelemetry[i].connected &&
        nodesTelemetry[i].lastReceivedMillis > 0) {
      unsigned long lastRecv = nodesTelemetry[i].lastReceivedMillis;
      unsigned long elapsed = freshNow - lastRecv;
      if (elapsed > STALE_TIMEOUT_MS && elapsed < 60000UL) {
        Serial.printf(
            "⚠️ [GATEWAY-BLE] Node #%d timeout (no data for %lums). Disconnecting...\n",
            i + 1, elapsed);
        nodesTelemetry[i].connected = false;
        nodesTelemetry[i].lastReceivedMillis = 0;
        nodesTelemetry[i].lastSequence = 0;
        if (pNodeClients[i] != nullptr && pNodeClients[i]->isConnected()) {
          pNodeClients[i]->disconnect();
        }
        resumeBleScanIfNeeded();
      }
    }
  }

  // Periodic MQTT Publish of consolidated IMU streams (10Hz)
  if (now - lastMqttStreamTime >= MQTT_STREAM_INTERVAL_MS) {
    lastMqttStreamTime = now;
    publishImuStream();
  }

  // 🌟 Auto-resume BLE Scan watchdog every 1s if any node is disconnected
  static unsigned long lastBleScanWatchdog = 0;
  if (now - lastBleScanWatchdog >= 1000) {
    lastBleScanWatchdog = now;
    resumeBleScanIfNeeded();
  }

  // 🌟 Periodic Gateway Heartbeat to Serial every 2 seconds
  static unsigned long lastGatewayHeartbeat = 0;
  if (now - lastGatewayHeartbeat >= 2000) {
    lastGatewayHeartbeat = now;
    int connectedCount = 0;
    char nodesSummary[64] = "";
    for (int i = 0; i < MAX_NODES; i++) {
      if (nodesTelemetry[i].connected) {
        connectedCount++;
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "#%d ", i + 1);
        strcat(nodesSummary, tmp);
      }
    }
    if (connectedCount == 0)
      strcpy(nodesSummary, "None");

    Serial.printf("🌐 [GATEWAY] Wi-Fi: %s (%s) | MQTT: %s | BLE: %d/4 [%s] | "
                  "Heap: %uB | Uptime: %lus\n",
                  (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE",
                  (WiFi.status() == WL_CONNECTED)
                      ? WiFi.localIP().toString().c_str()
                      : "No IP",
                  mqttClient.connected() ? "CONNECTED" : "DISCONNECTED",
                  connectedCount, nodesSummary,
                  ESP.getFreeHeap(), now / 1000);
  }
}

#else
#error                                                                         \
    "Please select an environment: 'gateway' or 'imu_node_1'..'imu_node_4' in platformio.ini"
#endif