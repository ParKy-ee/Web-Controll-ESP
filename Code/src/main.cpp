/**
 * ==============================================================================
 * Project: PlanKO - Smart Plank Training System
 * Hardware: ESP32-C3 (DevKitM-1 / SuperMini)
 *
 * Supports 2 Unified Roles via PlatformIO build flags:
 *   1. ROLE_GATEWAY:
 *      - BLE Central (connects to up to 4 IMU Nodes via BLE GATT)
 *      - Wi-Fi Station + local Dataset HTTP Server on Port 80
 *      - 16-pad Smart Mat Switch Inputs + Debounce + Serial Simulator
 *      - (Optional) MAX30102 Pulse Oximeter
 *      - Broadcasts consolidated IMU + Touch events to Expo App
 *
 *   2. ROLE_NODE:
 *      - BLE Peripheral (Advertises as PlanKO_IMU_1 .. PlanKO_IMU_4)
 *      - Reads MPU-6050 6-DOF IMU over I2C (SDA=8, SCL=9)
 *      - Computes Pitch, Roll, Yaw via Complementary Filter
 *      - Transmits telemetry and raw IMU packets via BLE Notifications
 * ==============================================================================
 */

#include "common_types.h"
#include <Arduino.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <stdlib.h>


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
#define I2C_SDA_PIN 8
#endif

#ifndef I2C_SCL_PIN
#define I2C_SCL_PIN 9
#endif

// Sampling frequency: 20Hz (50ms) - Optimal for BLE & Wi-Fi coexistence and
// queue stability
const unsigned long SAMPLE_INTERVAL_MS = 50;

Mpu6050Reader imu;
NimBLEServer *pServer = nullptr;
NimBLECharacteristic *pImuCharacteristic = nullptr;
bool bleConnected = false;
uint16_t packetSequence = 0;
unsigned long lastSampleTime = 0;
unsigned long lastSerialPrintTime = 0;

#define NODE_LOG(x)                                                            \
  do {                                                                         \
    if (Serial && Serial.availableForWrite() > 32)                             \
      Serial.print(x);                                                         \
  } while (0)
#define NODE_LOGLN(x)                                                          \
  do {                                                                         \
    if (Serial && Serial.availableForWrite() > 32)                             \
      Serial.println(x);                                                       \
  } while (0)
#define NODE_LOGF(...)                                                         \
  do {                                                                         \
    if (Serial && Serial.availableForWrite() > 32)                             \
      Serial.printf(__VA_ARGS__);                                              \
  } while (0)

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
    NODE_LOGF("🔌 [BLE] Gateway Disconnected! conn=%u. Restarting Adv...\n",
              desc->conn_handle);
    NimBLEDevice::startAdvertising();
  }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
  void onSubscribe(NimBLECharacteristic *pChar, ble_gap_conn_desc *desc,
                   uint16_t subVal) override {
    NODE_LOGF(
        "🔔 [NODE-BLE] Client %d subscription status changed: subVal = %u\n",
        desc->conn_handle, subVal);
  }
};

void printNodeBanner() {
  NODE_LOGLN(
      "\n****************************************************************");
  NODE_LOGF("🤖 [PlanKO IMU Node #%d] Firmware Started!\n", NODE_ID);
  NODE_LOGF("📡 BLE Advertising Name: PlanKO_IMU_%d\n", NODE_ID);
  NODE_LOGF("🔌 I2C Pins: SDA = GPIO %d, SCL = GPIO %d\n", I2C_SDA_PIN,
            I2C_SCL_PIN);
  NODE_LOGLN("💡 Type 'CAL' in Serial Monitor to re-calibrate IMU sensor");
  NODE_LOGLN(
      "****************************************************************\n");
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
    const bool sampleReady =
        imu.isDetected ? imu.update() : (imu.update() && imu.isDetected);
    if (sampleReady && pImuCharacteristic != nullptr &&
        (bleConnected || pImuCharacteristic->getSubscribedCount() > 0)) {
      const uint16_t sequence = ++packetSequence;
      const uint32_t sampleTime = millis();

      ImuPacket pkt;
      pkt.nodeId = NODE_ID;
      pkt.sequence = sequence;
      pkt.timestamp = sampleTime;
      pkt.pitch = (int16_t)(imu.pitch * 100.0f);
      pkt.roll = (int16_t)(imu.roll * 100.0f);
      pkt.yaw = (int16_t)(imu.yaw * 100.0f);
      pkt.ax = (int16_t)(imu.ax * 100.0f);
      pkt.ay = (int16_t)(imu.ay * 100.0f);
      pkt.az = (int16_t)(imu.az * 100.0f);
      pkt.status = imu.isCalibrated ? 1 : 0;

      pImuCharacteristic->notify((uint8_t *)&pkt, sizeof(pkt));

      RawImuPacket rawPkt;
      rawPkt.nodeId = NODE_ID;
      rawPkt.sequence = sequence;
      rawPkt.timestamp = sampleTime;
      rawPkt.ax = imu.rawAx;
      rawPkt.ay = imu.rawAy;
      rawPkt.az = imu.rawAz;
      rawPkt.gx = imu.rawGx;
      rawPkt.gy = imu.rawGy;
      rawPkt.gz = imu.rawGz;
      rawPkt.marker = RAW_IMU_PACKET_MARKER | (imu.isCalibrated ? 1 : 0);
      pImuCharacteristic->notify((uint8_t *)&rawPkt, sizeof(rawPkt));
    }
  }

  // Print telemetry to Serial once every 2 seconds
  if (now - lastSerialPrintTime >= 2000) {
    lastSerialPrintTime = now;
    if (Serial && Serial.availableForWrite() > 64) {
      size_t subs =
          pImuCharacteristic ? pImuCharacteristic->getSubscribedCount() : 0;
      uint16_t hdl = pImuCharacteristic ? pImuCharacteristic->getHandle() : 0;
      if (imu.isDetected) {
        Serial.printf("📊 [Node #%d][REAL IMU] Pitch: %5.1f° | Roll: %5.1f° | "
                      "BLE: %s (Subs: %u, hdl: %u) | Seq: %u\n",
                      NODE_ID, imu.pitch, imu.roll,
                      bleConnected ? "CONNECTED" : "ADVERTISING",
                      (unsigned int)subs, (unsigned int)hdl, packetSequence);
      } else {
        Serial.printf("⚠️ [Node #%d] MPU-6050 unavailable; waiting for real "
                      "sensor | BLE: %s\n",
                      NODE_ID, bleConnected ? "CONNECTED" : "ADVERTISING");
      }
    }
  }
}

#elif defined(ROLE_GATEWAY)
// ==============================================================================
// ----------------------------- ROLE: GATEWAY
// ----------------------------------
// ==============================================================================
#include "dataset_page.h"
#include <NimBLEDevice.h>
#include <Preferences.h>
#include <PubSubClient.h>
#include <WebServer.h>
#include <WiFi.h>
#include <esp_random.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>


// Wi-Fi Configuration
const char *WIFI_SSID = "MMm";
const char *WIFI_PASS = "123456789";

// MQTT Configuration (broker.emqx.io)
const char *MQTT_BROKER = "broker.emqx.io";
const uint16_t MQTT_PORT = 1883;
const char *TOPIC_IMU = "planko/imu";
const char *TOPIC_PAD = "planko/pad";
const char *TOPIC_STATUS = "planko/status";
char mqttClientId[32] = "PlanKO_Gateway_ESP32";

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// Static IP Configuration (Set to true only if router is fixed at 192.168.1.x)
const bool USE_STATIC_IP = false;
IPAddress STATIC_IP(192, 168, 1, 4);
IPAddress GATEWAY_IP(192, 168, 1, 1);
IPAddress SUBNET_MASK(255, 255, 255, 0);
IPAddress DNS_PRIMARY(192, 168, 1, 1);
IPAddress DNS_SECONDARY(8, 8, 8, 8);

WebServer webServer(80);
Preferences nodeConfigPreferences;

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

const size_t DATASET_HISTORY_CAPACITY = 256;
const size_t DATASET_BATCH_LIMIT = 16;
struct DatasetSample {
  uint32_t timestamp;
  uint16_t sequence;
  int16_t rawAx, rawAy, rawAz;
  int16_t rawGx, rawGy, rawGz;
  int16_t pitch, roll, yaw;
  int16_t ax, ay, az;
  uint8_t status;
  bool telemetryValid;
};

struct NodeDatasetHistory {
  DatasetSample samples[DATASET_HISTORY_CAPACITY];
  size_t nextIndex;
  size_t count;
  uint32_t bootId;
};

NodeDatasetHistory datasetHistory[MAX_NODES] = {};
portMUX_TYPE datasetMux = portMUX_INITIALIZER_UNLOCKED;
char nodeSensorUids[MAX_NODES][20] = {};
uint32_t gatewayBootId = 0;

struct NodeConfig {
  char name[32];
  char mountSite[20];
  char bodyPart[20];
  char side[8];
};

NodeConfig nodeConfigs[MAX_NODES] = {
    {"Node 1 - Neck", "neck", "neck", "unknown"},
    {"Node 2 - Back", "lumbar", "back", "unknown"},
    {"Node 3", "other", "other", "unknown"},
    {"Node 4", "other", "other", "unknown"}};

unsigned long lastMqttStreamTime = 0;
const unsigned long MQTT_STREAM_INTERVAL_MS =
    50; // 20Hz MQTT Stream (50ms interval)
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastBleScanTime = 0;
const unsigned long STALE_TIMEOUT_MS =
    15000; // Force disconnect if no data for 15s (prevents network jitter from
           // dropping BLE)
bool isScanning = false;

// Forward declaration
void sendPadPressEvent(const char *sensorId);
void publishImuStream();
void maintainMqttConnection();
void resumeBleScanIfNeeded(bool forceRestart = false);
void handleNodesApi();
void handleNodeConfigApi();
void handleSamplesApi();
void loadNodeConfigs();
void setupDatasetServer();

// BLE Client Callbacks
class GatewayClientCallbacks : public NimBLEClientCallbacks {
public:
  uint8_t nodeId;
  GatewayClientCallbacks(uint8_t id) : nodeId(id) {}

  void onConnect(NimBLEClient *pClient) override {
    const int idx = nodeId - 1;
    String uid = pClient->getPeerAddress().toString().c_str();
    uid.replace(":", "");
    uid.toLowerCase();
    uid.toCharArray(nodeSensorUids[idx], sizeof(nodeSensorUids[idx]));
    portENTER_CRITICAL(&datasetMux);
    datasetHistory[idx].nextIndex = 0;
    datasetHistory[idx].count = 0;
    datasetHistory[idx].bootId = esp_random();
    if (datasetHistory[idx].bootId == 0)
      datasetHistory[idx].bootId = 1;
    portEXIT_CRITICAL(&datasetMux);
    Serial.printf("✅ [GATEWAY-BLE] Connected to IMU Node #%d [%s]\n", nodeId,
                  pClient->getPeerAddress().toString().c_str());
    nodesTelemetry[idx].connected = true;
    nodesTelemetry[idx].lastReceivedMillis = millis();
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

  // Raw six-axis notification used only by the local Dataset recorder.
  if (length == sizeof(RawImuPacket) &&
      (pData[sizeof(RawImuPacket) - 1] & RAW_IMU_PACKET_MARKER_MASK) ==
          RAW_IMU_PACKET_MARKER) {
    const RawImuPacket *raw = (const RawImuPacket *)pData;
    if (raw->nodeId < 1 || raw->nodeId > MAX_NODES)
      return;
    const int idx = raw->nodeId - 1;
    DatasetSample sample = {};
    sample.timestamp = raw->timestamp;
    sample.sequence = raw->sequence;
    sample.rawAx = raw->ax;
    sample.rawAy = raw->ay;
    sample.rawAz = raw->az;
    sample.rawGx = raw->gx;
    sample.rawGy = raw->gy;
    sample.rawGz = raw->gz;
    nodesTelemetry[idx].gx = (raw->gx / 65.5f) * 0.01745329252f;
    nodesTelemetry[idx].gy = (raw->gy / 65.5f) * 0.01745329252f;
    nodesTelemetry[idx].gz = (raw->gz / 65.5f) * 0.01745329252f;
    if (nodesTelemetry[idx].lastSequence == raw->sequence) {
      sample.telemetryValid = true;
      sample.pitch = (int16_t)roundf(nodesTelemetry[idx].pitch * 100.0f);
      sample.roll = (int16_t)roundf(nodesTelemetry[idx].roll * 100.0f);
      sample.yaw = (int16_t)roundf(nodesTelemetry[idx].yaw * 100.0f);
      sample.ax = (int16_t)roundf(nodesTelemetry[idx].ax * 100.0f);
      sample.ay = (int16_t)roundf(nodesTelemetry[idx].ay * 100.0f);
      sample.az = (int16_t)roundf(nodesTelemetry[idx].az * 100.0f);
    }
    portENTER_CRITICAL(&datasetMux);
    NodeDatasetHistory &history = datasetHistory[idx];
    history.samples[history.nextIndex] = sample;
    history.nextIndex = (history.nextIndex + 1) % DATASET_HISTORY_CAPACITY;
    if (history.count < DATASET_HISTORY_CAPACITY)
      ++history.count;
    portEXIT_CRITICAL(&datasetMux);
    return;
  }

  if (length != sizeof(ImuPacket)) {
    Serial.printf(
        "⚠️ [GW-NOTIFY-LEN] Unexpected packet: %u bytes (expected %u)\n",
        (unsigned int)length, (unsigned int)sizeof(ImuPacket));
    return;
  }

  const ImuPacket *pkt = (const ImuPacket *)pData;
  if (pkt->nodeId < 1 || pkt->nodeId > MAX_NODES)
    return;

  int idx = pkt->nodeId - 1;
  NodeTelemetry &telemetry = nodesTelemetry[idx];

  const bool sequenceWrapped =
      telemetry.lastSequence > 60000 && pkt->sequence < 100;
  if (sequenceWrapped) {
    portENTER_CRITICAL(&datasetMux);
    datasetHistory[idx].nextIndex = 0;
    datasetHistory[idx].count = 0;
    datasetHistory[idx].bootId = esp_random();
    if (datasetHistory[idx].bootId == 0)
      datasetHistory[idx].bootId = 1;
    portEXIT_CRITICAL(&datasetMux);
    telemetry.lastSequence = 0;
  }

  if (telemetry.lastSequence == 0) {
    Serial.printf("🎉 [GW-RECV] First packet from Node #%d received! Seq: %u\n",
                  pkt->nodeId, pkt->sequence);
  }

  // Check packet drop sequence
  if (telemetry.lastSequence != 0) {
    const uint16_t sequenceDelta =
        static_cast<uint16_t>(pkt->sequence - telemetry.lastSequence);
    if (sequenceDelta > 1 && sequenceDelta < 0x8000) {
      const uint16_t dropped = sequenceDelta - 1;
      telemetry.totalDropped += dropped;
      Serial.printf(
          "⚠️ [GATEWAY-BLE] Node #%d Dropped %u packets (Seq: %u -> %u)\n",
          pkt->nodeId, dropped, telemetry.lastSequence, pkt->sequence);
    }
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
    } else if (advertisedDevice->isAdvertisingService(
                   NimBLEUUID(PLANKO_SERVICE_UUID))) {
      // Recognized via 128-bit PlanKO Service UUID even if name is deferred to
      // scan response
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

      Serial.printf("🔎 [GATEWAY-BLE] Found Node #%d ('%s') [%s] RSSI: %d\n",
                    id, name.c_str(),
                    advertisedDevice->getAddress().toString().c_str(),
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
    // 4. Scanner has been running for >= 3000ms (re-arms ESP32-C3 controller
    // GAP state to prevent stalls)
    bool shouldRestart = forceRestart || !pScan->isScanning() ||
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
      pScan->setActiveScan(false); // Passive scan: immediate ADV_IND reception
                                   // without RF collision
      pScan->start(0, nullptr, false);
    }
  } else {
    // All 4 nodes connected: stop scanning to give 100% radio bandwidth to data
    // & Wi-Fi
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

  Serial.printf("⏳ [GATEWAY-BLE] Connecting to Node #%d [%s]...\n", id,
                advDevice->getAddress().toString().c_str());

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
        Serial.printf("❌ [GATEWAY-BLE] Could not create client for Node #%d\n",
                      id);
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
  // Connection parameters: 20-40ms interval, latency 0 (instant delivery), supervision timeout 200 (2.0s)
  pClient->setConnectionParams(16, 32, 0, 200);
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
  NimBLERemoteDescriptor *pDesc =
      pRemoteChar->getDescriptor(NimBLEUUID((uint16_t)0x2902));
  Serial.printf("🔍 [GATEWAY-BLE] Node #%d Char handle: %d, CCCD handle: %d, "
                "canNotify: %d\n",
                id, pRemoteChar->getHandle(), pDesc ? pDesc->getHandle() : 0,
                pRemoteChar->canNotify());

  // Subscribe with response = false (standard non-blocking write to CCCD)
  bool subOk = pRemoteChar->subscribe(true, notifyCallback, false);
  Serial.printf("🔍 [GATEWAY-BLE] Node #%d subscribe returned: %s\n", id,
                subOk ? "TRUE" : "FALSE");

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
  // If no nodes are connected, throttle stream to 1Hz (every 1000ms) to prevent
  // broker flooding
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

      JsonArray gyr = n["gyro"].to<JsonArray>();
      gyr.add(round(nodesTelemetry[i].gx * 1000.0f) / 1000.0f);
      gyr.add(round(nodesTelemetry[i].gy * 1000.0f) / 1000.0f);
      gyr.add(round(nodesTelemetry[i].gz * 1000.0f) / 1000.0f);
    }
  }

  String output;
  serializeJson(doc, output);
  bool pubOk = mqttClient.publish(TOPIC_IMU, output.c_str());
  static unsigned long lastPubFailLog = 0;
  if (!pubOk && (millis() - lastPubFailLog > 3000)) {
    lastPubFailLog = millis();
    Serial.printf("⚠️ [MQTT] Publish to '%s' failed! rc=%d\n", TOPIC_IMU,
                  mqttClient.state());
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

bool isAllowedConfigValue(const String &value, const char *const allowed[],
                          size_t count) {
  for (size_t i = 0; i < count; ++i) {
    if (value == allowed[i])
      return true;
  }
  return false;
}

void loadNodeConfigs() {
  static const char *const sites[] = {"wrist",  "neck",   "chest", "upper_back",
                                      "lumbar", "pelvis", "head",  "other"};
  static const char *const bodyParts[] = {"neck", "back", "wrist", "chest",
                                          "other"};
  static const char *const sides[] = {"unknown", "left", "right"};
  nodeConfigPreferences.begin("node_cfg", true);
  for (int i = 0; i < MAX_NODES; ++i) {
    char key[16];
    snprintf(key, sizeof(key), "name%d", i + 1);
    String name = nodeConfigPreferences.getString(key, nodeConfigs[i].name);
    if (name.length() > 0)
      name.toCharArray(nodeConfigs[i].name, sizeof(nodeConfigs[i].name));

    snprintf(key, sizeof(key), "site%d", i + 1);
    String site =
        nodeConfigPreferences.getString(key, nodeConfigs[i].mountSite);
    if (isAllowedConfigValue(site, sites, sizeof(sites) / sizeof(sites[0])))
      site.toCharArray(nodeConfigs[i].mountSite,
                       sizeof(nodeConfigs[i].mountSite));

    snprintf(key, sizeof(key), "body%d", i + 1);
    String body = nodeConfigPreferences.getString(key, nodeConfigs[i].bodyPart);
    if (isAllowedConfigValue(body, bodyParts,
                             sizeof(bodyParts) / sizeof(bodyParts[0])))
      body.toCharArray(nodeConfigs[i].bodyPart,
                       sizeof(nodeConfigs[i].bodyPart));

    snprintf(key, sizeof(key), "side%d", i + 1);
    String side = nodeConfigPreferences.getString(key, nodeConfigs[i].side);
    if (isAllowedConfigValue(side, sides, sizeof(sides) / sizeof(sides[0])))
      side.toCharArray(nodeConfigs[i].side, sizeof(nodeConfigs[i].side));
  }
  nodeConfigPreferences.end();
}

void handleNodesApi() {
  JsonDocument doc;
  doc["ip"] = WiFi.localIP().toString();
  doc["uptime_ms"] = millis();
  doc["mqtt"] = mqttClient.connected();
  JsonArray list = doc["nodes"].to<JsonArray>();
  int connectedCount = 0;
  for (int i = 0; i < MAX_NODES; ++i) {
    const uint32_t lastReceived = nodesTelemetry[i].lastReceivedMillis;
    const bool hasData =
        lastReceived > 0 && millis() - lastReceived < STALE_TIMEOUT_MS;
    uint32_t bootId;
    portENTER_CRITICAL(&datasetMux);
    bootId = datasetHistory[i].bootId;
    portEXIT_CRITICAL(&datasetMux);
    if (nodesTelemetry[i].connected)
      ++connectedCount;
    JsonObject node = list.add<JsonObject>();
    node["nodeId"] = i + 1;
    node["name"] = nodeConfigs[i].name;
    node["mount_site"] = nodeConfigs[i].mountSite;
    node["body_part"] = nodeConfigs[i].bodyPart;
    node["side"] = nodeConfigs[i].side;
    node["sensor_uid"] = nodeSensorUids[i];
    node["connected"] = nodesTelemetry[i].connected;
    node["hasData"] = hasData;
    node["latest_seq"] = nodesTelemetry[i].lastSequence;
    node["total_dropped"] = nodesTelemetry[i].totalDropped;
    node["boot_id"] = bootId;
  }
  doc["connectedNodes"] = connectedCount;
  String output;
  serializeJson(doc, output);
  webServer.send(200, "application/json", output);
}

void handleNodeConfigApi() {
  static const char *const sites[] = {"wrist",  "neck",   "chest", "upper_back",
                                      "lumbar", "pelvis", "head",  "other"};
  static const char *const bodyParts[] = {"neck", "back", "wrist", "chest",
                                          "other"};
  static const char *const sides[] = {"unknown", "left", "right"};
  if (!webServer.hasArg("node_id")) {
    webServer.send(400, "application/json",
                   "{\"message\":\"node_id is required\"}");
    return;
  }
  const int nodeId = webServer.arg("node_id").toInt();
  if (nodeId < 1 || nodeId > MAX_NODES) {
    webServer.send(400, "application/json",
                   "{\"message\":\"node_id must be 1..4\"}");
    return;
  }
  const String name = webServer.arg("name");
  const String site = webServer.arg("mount_site");
  const String body = webServer.arg("body_part");
  const String side = webServer.arg("side");
  if (name.length() == 0 || name.length() >= sizeof(nodeConfigs[0].name) ||
      !isAllowedConfigValue(site, sites, sizeof(sites) / sizeof(sites[0])) ||
      !isAllowedConfigValue(body, bodyParts,
                            sizeof(bodyParts) / sizeof(bodyParts[0])) ||
      !isAllowedConfigValue(side, sides, sizeof(sides) / sizeof(sides[0]))) {
    webServer.send(400, "application/json",
                   "{\"message\":\"invalid node configuration\"}");
    return;
  }

  char nameKey[16], siteKey[16], bodyKey[16], sideKey[16];
  snprintf(nameKey, sizeof(nameKey), "name%d", nodeId);
  snprintf(siteKey, sizeof(siteKey), "site%d", nodeId);
  snprintf(bodyKey, sizeof(bodyKey), "body%d", nodeId);
  snprintf(sideKey, sizeof(sideKey), "side%d", nodeId);
  nodeConfigPreferences.begin("node_cfg", false);
  const size_t savedName = nodeConfigPreferences.putString(nameKey, name);
  const size_t savedSite = nodeConfigPreferences.putString(siteKey, site);
  const size_t savedBody = nodeConfigPreferences.putString(bodyKey, body);
  const size_t savedSide = nodeConfigPreferences.putString(sideKey, side);
  nodeConfigPreferences.end();
  if (savedName == 0 || savedSite == 0 || savedBody == 0 || savedSide == 0) {
    webServer.send(500, "application/json",
                   "{\"message\":\"could not save node configuration\"}");
    return;
  }

  name.toCharArray(nodeConfigs[nodeId - 1].name,
                   sizeof(nodeConfigs[nodeId - 1].name));
  site.toCharArray(nodeConfigs[nodeId - 1].mountSite,
                   sizeof(nodeConfigs[nodeId - 1].mountSite));
  body.toCharArray(nodeConfigs[nodeId - 1].bodyPart,
                   sizeof(nodeConfigs[nodeId - 1].bodyPart));
  side.toCharArray(nodeConfigs[nodeId - 1].side,
                   sizeof(nodeConfigs[nodeId - 1].side));
  webServer.send(200, "application/json", "{\"ok\":true}");
}

bool isSequenceAfter(uint16_t sequence, uint16_t afterSequence) {
  return static_cast<int16_t>(sequence - afterSequence) > 0;
}

void handleSamplesApi() {
  static DatasetSample snapshot[MAX_NODES][DATASET_BATCH_LIMIT];
  static size_t snapshotCount[MAX_NODES] = {};
  static uint32_t snapshotBootId[MAX_NODES] = {};
  static bool snapshotOverflow[MAX_NODES] = {};

  portENTER_CRITICAL(&datasetMux);
  for (int i = 0; i < MAX_NODES; ++i) {
    const NodeDatasetHistory &history = datasetHistory[i];
    snapshotCount[i] = history.count < DATASET_BATCH_LIMIT
                           ? history.count
                           : DATASET_BATCH_LIMIT;
    snapshotOverflow[i] = history.count > DATASET_BATCH_LIMIT;
    snapshotBootId[i] = history.bootId;
    const size_t oldest =
        (history.nextIndex + DATASET_HISTORY_CAPACITY - snapshotCount[i]) %
        DATASET_HISTORY_CAPACITY;
    for (size_t j = 0; j < snapshotCount[i]; ++j) {
      snapshot[i][j] = history.samples[(oldest + j) % DATASET_HISTORY_CAPACITY];
    }
  }
  portEXIT_CRITICAL(&datasetMux);

  JsonDocument doc;
  doc["gateway_boot_id"] = gatewayBootId;
  JsonArray nodes = doc["nodes"].to<JsonArray>();
  const uint32_t now = millis();
  for (int i = 0; i < MAX_NODES; ++i) {
    char bootKey[20], sequenceKey[20];
    snprintf(bootKey, sizeof(bootKey), "n%d_boot_id", i + 1);
    snprintf(sequenceKey, sizeof(sequenceKey), "n%d_after_seq", i + 1);
    const bool hasCursor =
        webServer.hasArg(bootKey) && webServer.hasArg(sequenceKey) &&
        static_cast<uint32_t>(strtoul(webServer.arg(bootKey).c_str(), nullptr,
                                      10)) == snapshotBootId[i];
    const uint16_t afterSequence =
        hasCursor ? static_cast<uint16_t>(strtoul(
                        webServer.arg(sequenceKey).c_str(), nullptr, 10))
                  : 0;

    bool dropped = !hasCursor && snapshotOverflow[i];
    if (hasCursor && snapshotCount[i] > 0) {
      const uint16_t expected = static_cast<uint16_t>(afterSequence + 1);
      bool expectedStillBuffered = false;
      for (size_t j = 0; j < snapshotCount[i]; ++j) {
        if (snapshot[i][j].sequence == expected)
          expectedStillBuffered = true;
      }
      dropped = isSequenceAfter(snapshot[i][snapshotCount[i] - 1].sequence,
                                afterSequence) &&
                !expectedStillBuffered;
    }

    JsonObject node = nodes.add<JsonObject>();
    const uint32_t lastReceived = nodesTelemetry[i].lastReceivedMillis;
    node["nodeId"] = i + 1;
    node["name"] = nodeConfigs[i].name;
    node["mount_site"] = nodeConfigs[i].mountSite;
    node["body_part"] = nodeConfigs[i].bodyPart;
    node["side"] = nodeConfigs[i].side;
    node["sensor_uid"] = nodeSensorUids[i];
    node["connected"] = nodesTelemetry[i].connected;
    node["hasData"] = lastReceived > 0 && now - lastReceived < STALE_TIMEOUT_MS;
    node["boot_id"] = snapshotBootId[i];
    node["latest_seq"] =
        snapshotCount[i] ? snapshot[i][snapshotCount[i] - 1].sequence : 0;
    node["total_dropped"] = nodesTelemetry[i].totalDropped;
    node["dropped"] = dropped;
    JsonArray samples = node["samples"].to<JsonArray>();
    for (size_t j = 0; j < snapshotCount[i]; ++j) {
      const DatasetSample &source = snapshot[i][j];
      if (hasCursor && !isSequenceAfter(source.sequence, afterSequence))
        continue;
      JsonObject sample = samples.add<JsonObject>();
      sample["sample_ms"] = source.timestamp;
      sample["seq"] = source.sequence;
      sample["raw_ax"] = source.rawAx;
      sample["raw_ay"] = source.rawAy;
      sample["raw_az"] = source.rawAz;
      sample["raw_gx"] = source.rawGx;
      sample["raw_gy"] = source.rawGy;
      sample["raw_gz"] = source.rawGz;
      const float rawAxG = source.rawAx / 8192.0f;
      const float rawAyG = source.rawAy / 8192.0f;
      const float rawAzG = source.rawAz / 8192.0f;
      const float accelX =
          source.telemetryValid ? source.ax / 100.0f : rawAxG * 9.80665f;
      const float accelY =
          source.telemetryValid ? source.ay / 100.0f : rawAyG * 9.80665f;
      const float accelZ =
          source.telemetryValid ? source.az / 100.0f : rawAzG * 9.80665f;
      sample["ax"] = accelX;
      sample["ay"] = accelY;
      sample["az"] = accelZ;
      sample["gx"] = (source.rawGx / 65.5f) * 0.01745329252f;
      sample["gy"] = (source.rawGy / 65.5f) * 0.01745329252f;
      sample["gz"] = (source.rawGz / 65.5f) * 0.01745329252f;
      const float accelNormG =
          sqrtf(accelX * accelX + accelY * accelY + accelZ * accelZ) / 9.80665f;
      const float gxDps = source.rawGx / 65.5f;
      const float gyDps = source.rawGy / 65.5f;
      const float gzDps = source.rawGz / 65.5f;
      const float gyroNormDps =
          sqrtf(gxDps * gxDps + gyDps * gyDps + gzDps * gzDps);
      sample["accel_norm_g"] = accelNormG;
      sample["gyro_norm_dps"] = gyroNormDps;
      sample["steady"] =
          accelNormG >= 0.90f && accelNormG <= 1.10f && gyroNormDps <= 10.0f;
      sample["roll_deg"] = source.roll / 100.0f;
      sample["pitch_deg"] = source.pitch / 100.0f;
      sample["yaw_deg"] = source.yaw / 100.0f;
      sample["angle_valid"] = source.telemetryValid;
      sample["calibrated"] = source.status == 1;
      sample["valid"] = true;
      sample["status"] =
          source.status == 1 ? "real_calibrated" : "real_uncalibrated";
    }
  }
  String output;
  serializeJson(doc, output);
  webServer.send(200, "application/json", output);
}

void setupDatasetServer() {
  webServer.on("/", HTTP_GET, []() {
    webServer.send_P(200, "text/html; charset=utf-8", DATASET_PAGE);
  });
  webServer.on("/api/nodes", HTTP_GET, handleNodesApi);
  webServer.on("/api/node-config", HTTP_POST, handleNodeConfigApi);
  webServer.on("/samples", HTTP_GET, handleSamplesApi);
  webServer.onNotFound([]() {
    webServer.send(404, "application/json", "{\"error\":\"not_found\"}");
  });
  webServer.begin();
  Serial.printf("🌐 [DATASET] Web recorder at http://%s/\n",
                WiFi.localIP().toString().c_str());
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

  gatewayBootId = esp_random();
  if (gatewayBootId == 0)
    gatewayBootId = 1;
  loadNodeConfigs();

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
        s->stop(); // Pause BLE scan so Wi-Fi can reconnect without radio
                   // interference
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
                  WiFi.SSID(i).c_str(), WiFi.RSSI(i), WiFi.channel(i),
                  WiFi.encryptionType(i));
    if (WiFi.SSID(i) == WIFI_SSID) {
      targetChannel = WiFi.channel(i);
    }
  }
  WiFi.scanDelete();

  // Enable PMF (Protected Management Frames) for compatibility with modern
  // Wi-Fi routers
  wifi_config_t current_conf;
  esp_wifi_get_config(WIFI_IF_STA, &current_conf);
  current_conf.sta.pmf_cfg.capable = true;
  current_conf.sta.pmf_cfg.required = false;
  esp_wifi_set_config(WIFI_IF_STA, &current_conf);

  WiFi.setTxPower(WIFI_POWER_8_5dBm); // Low TX power prevents voltage drop on
                                      // ESP32-C3 DevKit during auth
  if (USE_STATIC_IP) {
    if (!WiFi.config(STATIC_IP, GATEWAY_IP, SUBNET_MASK, DNS_PRIMARY,
                     DNS_SECONDARY)) {
      Serial.println(
          "⚠️ [WIFI] Static IP configuration failed; DHCP may be used instead.");
    }
    Serial.printf("🌐 [WIFI] Requested Gateway IP: %s\n",
                  STATIC_IP.toString().c_str());
  } else {
    Serial.println("🌐 [WIFI] Using DHCP (Automatic IP, Gateway & DNS)...");
  }
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
  setupDatasetServer();

  // Initialize MQTT Client
  snprintf(mqttClientId, sizeof(mqttClientId), "PlanKO_GW_%08X",
           (uint32_t)ESP.getEfuseMac());
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setBufferSize(2048);
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
  pScan->setAdvertisedDeviceCallbacks(new AdvertisedDeviceCallbacks(),
                                      true); // wantDuplicates = true
  pScan->setActiveScan(
      false); // Passive scan: immediate ADV_IND reception without RF contention
  pScan->setDuplicateFilter(false);
  pScan->setMaxResults(0);
  pScan->setInterval(160); // 100ms interval
  pScan->setWindow(120);   // 75ms window (75% duty cycle)

  Serial.println("👉 [INIT] Step 4: Starting BLE Continuous Scan...");
  Serial.flush();
  pScan->start(0, nullptr, false);
  isScanning = true;
  Serial.println(
      "🎉 [INIT] Setup completed! Scanner active, MQTT connected.\n");
  Serial.flush();
}

void loop() {
  maintainMqttConnection();
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  webServer.handleClient();
  handlePadInputs();
  handleSerialSimulator();

  unsigned long now = millis();

  // Check for pending BLE connections (one at a time to keep MQTT & Wi-Fi
  // responsive)
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
        Serial.printf("⚠️ [GATEWAY-BLE] Node #%d timeout (no data for %lums). "
                      "Disconnecting...\n",
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
                  connectedCount, nodesSummary, ESP.getFreeHeap(), now / 1000);
  }
}

#else
#error                                                                         \
    "Please select an environment: 'gateway' or 'imu_node_1'..'imu_node_4' in platformio.ini"
#endif
