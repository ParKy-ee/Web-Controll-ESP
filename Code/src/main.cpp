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

#if (NODE_ID == 1)
#include <MAX30105.h>
#include <heartRate.h>
MAX30105 max30102;
bool max30102Detected = false;
uint32_t currentRed = 0, currentIr = 0;
float currentBpm = 0.0f, beatAvg = 0.0f;
uint8_t currentSpo2 = 0;
bool currentFinger = false;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE] = {}, rateSpot = 0;
unsigned long lastBeat = 0, lastBioNotifyTime = 0;
const unsigned long BIO_NOTIFY_INTERVAL_MS = 250;
bool initMax30102() {
  if (!max30102.begin(Wire, I2C_SPEED_STANDARD)) return false;
  max30102.setup(0x1F, 4, 2, 100, 411, 4096);
  max30102.setPulseAmplitudeGreen(0);
  return true;
}
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

#if (NODE_ID == 1)
  // 1.1 Initialize MAX30102 Pulse Oximeter on I2C bus (SDA=8, SCL=9)
  max30102Detected = initMax30102();
#endif

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

#if (NODE_ID == 1)
  // Background retry if MAX30102 was not ready at boot
  static unsigned long lastMaxRetryTime = 0;
  if (!max30102Detected && (now - lastMaxRetryTime >= 2000)) {
    lastMaxRetryTime = now;
    max30102Detected = initMax30102();
  }

  // Read MAX30102 sensor and process pulse oximetry
  if (max30102Detected) {
    max30102.check();
    while (max30102.available()) {
      currentRed = max30102.getFIFORed();
      currentIr = max30102.getFIFOIR();
      max30102.nextSample();
    }

    // Direct read fallback if FIFO empty
    if (currentIr == 0) {
      currentIr = max30102.getIR();
      currentRed = max30102.getRed();
    }

    // Finger & Heartbeat detection logic
    bool noRecentBeat = (lastBeat == 0 || (now - lastBeat > 2500));

    // If IR is low or no heartbeat detected recently, decay values to 0
    if (currentIr < 50000 || noRecentBeat) {
      currentBpm = 0.0f;
      currentSpo2 = 0;
      if (now - lastBeat > 3000) {
        currentFinger = false;
        for (byte x = 0; x < RATE_SIZE; x++)
          rates[x] = 0;
        rateSpot = 0;
        beatAvg = 0.0f;
      }
    }

    if (currentIr >= 50000) {
      if (checkForBeat(currentIr)) {
        long delta = now - lastBeat;
        lastBeat = now;
        if (delta > 270 && delta < 2000) { // Valid HR: 30-220 BPM
          float beatsPerMinute = 60.0f / (delta / 1000.0f);
          rates[rateSpot++] = (byte)round(beatsPerMinute);
          rateSpot %= RATE_SIZE;

          int sum = 0;
          byte count = 0;
          for (byte x = 0; x < RATE_SIZE; x++) {
            if (rates[x] > 0) {
              sum += rates[x];
              count++;
            }
          }

          // Require at least 2 consecutive rhythmic beats to confirm a real
          // human finger
          if (count >= 2) {
            beatAvg = (float)sum / count;
            currentBpm = beatAvg;
            currentFinger = true;

            if (currentIr > 0 && currentRed > 0) {
              float ratio = (float)currentRed / (float)currentIr;
              int estimatedSpo2 = (int)(110.0f - 25.0f * ratio);
              if (estimatedSpo2 > 100)
                estimatedSpo2 = 100;
              if (estimatedSpo2 < 85)
                estimatedSpo2 = 85;
              currentSpo2 = (uint8_t)estimatedSpo2;
            }
          }
        }
      }
    }

    // Transmit BioPacket via BLE Notifications at 4Hz (every 250ms)
    if (now - lastBioNotifyTime >= BIO_NOTIFY_INTERVAL_MS) {
      lastBioNotifyTime = now;
      if (pImuCharacteristic != nullptr &&
          (bleConnected || pImuCharacteristic->getSubscribedCount() > 0)) {
        BioPacket bioPkt;
        bioPkt.nodeId = NODE_ID;
        bioPkt.sequence = packetSequence;
        bioPkt.timestamp = now;
        bioPkt.bpm = (uint16_t)round(currentBpm * 10.0f);
        bioPkt.spo2 = currentSpo2;
        bioPkt.finger = currentFinger ? 1 : 0;
        bioPkt.red = currentRed;
        bioPkt.ir = currentIr;
        bioPkt.marker = BIO_PACKET_MARKER;
        pImuCharacteristic->notify((uint8_t *)&bioPkt, sizeof(bioPkt));
      }
    }
  }
#endif

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
#if (NODE_ID == 1)
      if (max30102Detected) {
        Serial.printf("❤️ [Node #%d][MAX30102] IR: %u | Red: %u | BPM: %5.1f | "
                      "SpO2: %3u%% | Finger: %s\n",
                      NODE_ID, currentIr, currentRed, currentBpm,
                      (unsigned int)currentSpo2,
                      currentFinger ? "DETECTED" : "NO FINGER");
      } else {
        Serial.printf("⚠️ [Node #%d][MAX30102] Not responding at 0x57 (Check "
                      "SDA=8, SCL=9, 3.3V, GND)\n",
                      NODE_ID);
      }
#endif
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
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

// Wi-Fi Configuration
const char *WIFI_SSID = "MMm";
const char *WIFI_PASS = "123456789";

// Node -> BLE -> Gateway -> local Docker Mosquitto -> subscribers.
// Update this destination if the PC's DHCP-assigned Wi-Fi address changes.
const char *MQTT_BROKER = "10.178.191.76";
const uint16_t MQTT_PORT = 1883;
const char *TOPIC_IMU = "planko/imu";
const char *TOPIC_PAD = "planko/pad";
const char *TOPIC_STATUS = "planko/status";
const char *TOPIC_PRESSURE = "planko/pressure";
const char *TOPIC_BIO = "planko/bio";
char mqttClientId[32] = "PlanKO_Gateway_ESP32";
char mqttCommandTopic[80] = {};
char pendingMqttPadCommand[8] = {};
bool pendingMqttPadEvent = false;

// ==============================================================================
// RFP-602 Thin-Film Pressure Sensor Configuration
// ==============================================================================
#ifndef RFP602_PIN
#define RFP602_PIN 0 // ADC1_CH0 on ESP32-C3
#endif

const int RFP602_PRESS_THRESHOLD =
    300; // ADC threshold (0-4095) for detecting press
int rfp602RawValue = 0;
float rfp602Voltage = 0.0f;
bool rfp602Pressed = false;
bool rfp602LastPressedState = false;
unsigned long lastRfp602SampleTime = 0;
const unsigned long RFP602_SAMPLE_INTERVAL_MS =
    50; // Read ADC at 20Hz (every 50ms)
unsigned long lastRfp602MqttTime = 0;
const unsigned long RFP602_MQTT_INTERVAL_MS =
    250; // 4Hz MQTT stream (every 250ms = 4 packets/sec)

WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

// DHCP supplies the local address, route and DNS for the current Wi-Fi network.
// MQTT subscribers identify the Gateway by client ID, not by its local IP.

WebServer webServer(80);
Preferences nodeConfigPreferences;

// 16-Pad GPIO Hardware Configuration (ESP32-C3 Safe Pins)
// Note: L1 is set to -1 because GPIO 0 is used by analog RFP-602 pressure
// sensor
struct PadConfig {
  const char *name;
  int8_t pin;
};

const PadConfig PAD_CONFIGS[] = {
    {"L1", -1}, {"L2", 1},  {"L3", 2},  {"L4", 3},  {"L5", 4},  {"L6", 5},
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

// BLE central supports four node slots; this Gateway session expects Nodes 1 and 2.
const int MAX_NODES = 4;
const uint8_t EXPECTED_NODE_IDS[] = {1, 2};
const size_t EXPECTED_NODE_COUNT =
    sizeof(EXPECTED_NODE_IDS) / sizeof(EXPECTED_NODE_IDS[0]);
NodeTelemetry nodesTelemetry[MAX_NODES];
NimBLEClient *pNodeClients[MAX_NODES] = {nullptr, nullptr, nullptr, nullptr};
struct PendingBleNode {
  NimBLEAddress address;
  bool pending = false;
  bool connecting = false;
  uint32_t retryAfter = 0;
};
PendingBleNode pendingBleNodes[MAX_NODES];
portMUX_TYPE bleConnectMux = portMUX_INITIALIZER_UNLOCKED;
std::atomic<bool> bleScanRestartRequested{false};

bool isExpectedBleNode(int nodeId) {
  for (size_t i = 0; i < EXPECTED_NODE_COUNT; ++i) {
    if (EXPECTED_NODE_IDS[i] == nodeId)
      return true;
  }
  return false;
}

int connectedExpectedBleNodeCount() {
  int connectedCount = 0;
  for (size_t i = 0; i < EXPECTED_NODE_COUNT; ++i) {
    const int idx = EXPECTED_NODE_IDS[i] - 1;
    if (nodesTelemetry[idx].connected)
      ++connectedCount;
  }
  return connectedCount;
}

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
const size_t MQTT_SAMPLE_BATCH_LIMIT = 4;
uint16_t mqttPublishedSequence[MAX_NODES] = {};
uint32_t mqttPublishedBootId[MAX_NODES] = {};
bool mqttPublishedCursorValid[MAX_NODES] = {};
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
    50; // 20 Hz consolidated stream; one message includes all nodes.
unsigned long lastMqttReconnectAttempt = 0;
unsigned long lastBleScanTime = 0;
const unsigned long STALE_TIMEOUT_MS =
    15000; // Force disconnect if no data for 15s (prevents network jitter from
           // dropping BLE)
bool isScanning = false;

// Forward declaration
void sendPadPressEvent(const char *sensorId);
void gatewayMqttCallback(char *topic, byte *payload, unsigned int length);
void publishImuStream();
void readRfp602Sensor();
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

    // The NimBLE host owns callback execution. Scanner/client lifecycle stays
    // in loop(); callbacks never delete objects used by a connection attempt.
    bleScanRestartRequested.store(true);
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

  // Biomarker notification (MAX30102 on Node 1)
  if (length == sizeof(BioPacket) &&
      (pData[sizeof(BioPacket) - 1] & BIO_PACKET_MARKER_MASK) ==
          BIO_PACKET_MARKER) {
    const BioPacket *bio = (const BioPacket *)pData;
    if (bio->nodeId >= 1 && bio->nodeId <= MAX_NODES) {
      int idx = bio->nodeId - 1;
      nodesTelemetry[idx].bpm = bio->bpm / 10.0f;
      nodesTelemetry[idx].spo2 = bio->spo2;
      nodesTelemetry[idx].finger = (bio->finger != 0);
      nodesTelemetry[idx].rawRed = bio->red;
      nodesTelemetry[idx].rawIr = bio->ir;
      nodesTelemetry[idx].lastReceivedMillis = millis();

      static unsigned long lastBioLog = 0;
      if (millis() - lastBioLog >= 1000) {
        lastBioLog = millis();
        Serial.printf("📥 [GW-BIO] Node #%d | IR: %lu | Red: %lu | BPM: %.1f | "
                      "SpO2: %u%% | Finger: %s\n",
                      bio->nodeId, (unsigned long)bio->ir,
                      (unsigned long)bio->red, nodesTelemetry[idx].bpm,
                      nodesTelemetry[idx].spo2,
                      nodesTelemetry[idx].finger ? "YES" : "NO");
      }
    }
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
    sample.status = raw->marker & 1;
    nodesTelemetry[idx].lastRawReceivedMillis = millis();
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
                  static_cast<unsigned>(telemetry.totalDropped));
  }
}

// Advertised Device Callback
class AdvertisedDeviceCallbacks : public NimBLEAdvertisedDeviceCallbacks {
  void onResult(NimBLEAdvertisedDevice *advertisedDevice) override {
    int id = 0;
    std::string name = advertisedDevice->getName();
    if (name.rfind("PlanKO_IMU_", 0) == 0 && name.length() == 12) {
      id = name[11] - '0';
    }

    // Only the configured Nodes participate in this Gateway session.
    if (isExpectedBleNode(id)) {
      int idx = id - 1;
      // If node is already connected, ignore advertisement
      if (nodesTelemetry[idx].connected) {
        return;
      }

      // Copy only the address. Advertised objects belong to the scanner and
      // can disappear on clearResults(); no shared heap pointer crosses tasks.
      const NimBLEAddress address = advertisedDevice->getAddress();
      bool queued = false;
      const uint32_t now = millis();
      portENTER_CRITICAL(&bleConnectMux);
      PendingBleNode &pending = pendingBleNodes[idx];
      if (!pending.pending && !pending.connecting &&
          (pending.retryAfter == 0 ||
           static_cast<int32_t>(now - pending.retryAfter) >= 0)) {
        pending.address = address;
        pending.pending = true;
        queued = true;
      }
      portEXIT_CRITICAL(&bleConnectMux);
      if (queued) {
        Serial.printf("🔎 [GATEWAY-BLE] Found Node #%d ('%s') [%s] RSSI: %d\n",
                      id, name.c_str(), address.toString().c_str(),
                      advertisedDevice->getRSSI());
      }
    }
  }
};

void resumeBleScanIfNeeded(bool forceRestart) {
  const int connectedCount = connectedExpectedBleNodeCount();
  const bool missingExpectedNode =
      connectedCount < static_cast<int>(EXPECTED_NODE_COUNT);
  static unsigned long lastScanCycle = 0;
  static int lastExpectedConnectedCount = -1;

  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (!pScan)
    return;

  if (missingExpectedNode) {
    uint16_t desiredWindow = (connectedCount == 0) ? 120 : 32;
    unsigned long now = millis();

    // Restart scan if:
    // 1. Scanner is not currently running
    // 2. forceRestart requested (e.g. immediately on disconnect)
    // 3. Expected connected count changed (adjust 75%/20% scan duty cycle)
    // 4. Scanner has been running for >= 3000ms (re-arms ESP32-C3 controller
    // GAP state to prevent stalls)
    bool shouldRestart = forceRestart || !pScan->isScanning() ||
                         (connectedCount != lastExpectedConnectedCount) ||
                         (now - lastScanCycle >= 3000);

    if (shouldRestart) {
      lastScanCycle = now;
      lastExpectedConnectedCount = connectedCount;

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
    // All expected Nodes are connected. Stop scanning so the Gateway radio can
    // focus on the active BLE links and Wi-Fi; restart if an expected Node drops.
    if (pScan->isScanning()) {
      pScan->stop();
      pScan->clearResults();
    }
    lastExpectedConnectedCount = connectedCount;
  }
}

bool connectBlePeer(int idx, const NimBLEAddress &peerAddr) {
  const int id = idx + 1;
  // Each Node owns one client for its lifetime. Reusing another Node's client
  // leaves stale aliases in pNodeClients and routes callbacks to the wrong ID.
  NimBLEClient *pClient = pNodeClients[idx];
  if (pClient == nullptr) {
    pClient = NimBLEDevice::createClient();
    if (pClient == nullptr) {
      Serial.printf("❌ [GATEWAY-BLE] Could not create client for Node #%d\n", id);
      return false;
    }
    pClient->setClientCallbacks(new GatewayClientCallbacks(id), true);
    pNodeClients[idx] = pClient;
  }
  if (pClient->isConnected())
    return nodesTelemetry[idx].connected;
  // Connection parameters: 20-40ms interval, latency 2, supervision timeout 200
  // (2.0s)
  pClient->setConnectionParams(16, 32, 2, 200);
  pClient->setConnectTimeout(5);

  if (!pClient->connect(peerAddr, true)) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to connect to Node #%d (will retry)\n", id);
    return false;
  }

  NimBLERemoteService *pRemoteService =
      pClient->getService(PLANKO_SERVICE_UUID);
  if (pRemoteService == nullptr) {
    Serial.printf("❌ [GATEWAY-BLE] Failed to find service on Node #%d\n", id);
    pClient->disconnect();
    return false;
  }

  NimBLERemoteCharacteristic *pRemoteChar =
      pRemoteService->getCharacteristic(PLANKO_CHARACTERISTIC_UUID);
  if (pRemoteChar == nullptr || !pRemoteChar->canNotify()) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to find notify characteristic on Node #%d\n",
        id);
    pClient->disconnect();
    return false;
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

  if (!subOk || !pClient->isConnected()) {
    Serial.printf(
        "❌ [GATEWAY-BLE] Failed to subscribe to Node #%d notifications\n", id);
    pClient->disconnect();
    return false;
  }

  Serial.printf(
      "🎉 [GATEWAY-BLE] Successfully subscribed to Node #%d telemetry!\n", id);
  nodesTelemetry[idx].connected = true;
  nodesTelemetry[idx].lastReceivedMillis = millis();
  nodesTelemetry[idx].lastSequence = 0;
  return true;
}

void connectToNode(int idx) {
  NimBLEAddress peerAddr;
  portENTER_CRITICAL(&bleConnectMux);
  PendingBleNode &pending = pendingBleNodes[idx];
  if (!pending.pending || pending.connecting) {
    portEXIT_CRITICAL(&bleConnectMux);
    return;
  }
  peerAddr = pending.address;
  pending.pending = false;
  pending.connecting = true;
  portEXIT_CRITICAL(&bleConnectMux);

  Serial.printf("⏳ [GATEWAY-BLE] Connecting to Node #%d [%s]...\n", idx + 1,
                peerAddr.toString().c_str());
  NimBLEScan *pScan = NimBLEDevice::getScan();
  if (pScan && pScan->isScanning())
    pScan->stop();

  const bool connected = connectBlePeer(idx, peerAddr);
  const uint32_t retryAfter = connected ? 0 : millis() + 3000;
  portENTER_CRITICAL(&bleConnectMux);
  pending.connecting = false;
  pending.retryAfter = retryAfter;
  portEXIT_CRITICAL(&bleConnectMux);
  // Keep weaker/unavailable Nodes from monopolizing retries; the next loop can
  // connect a different pending Node during this Node's cooldown.
  resumeBleScanIfNeeded(true);
}

void maintainMqttConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }
  if (!mqttClient.connected()) {
    unsigned long now = millis();
    if (now - lastMqttReconnectAttempt > 5000) {
      lastMqttReconnectAttempt = now;
      Serial.printf("[MQTT] Connecting to %s:%u... ", MQTT_BROKER, MQTT_PORT);
      IPAddress brokerAddress;
      if (WiFi.hostByName(MQTT_BROKER, brokerAddress) != 1) {
        Serial.println("DNS lookup failed; checking DHCP network settings");
        Serial.printf("[WIFI] IP=%s Gateway=%s DNS1=%s DNS2=%s\n",
                      WiFi.localIP().toString().c_str(),
                      WiFi.gatewayIP().toString().c_str(),
                      WiFi.dnsIP(0).toString().c_str(),
                      WiFi.dnsIP(1).toString().c_str());
        lastMqttReconnectAttempt = millis();
        return;
      }
      mqttClient.setServer(brokerAddress, MQTT_PORT);
      // Connect with Last Will and Testament on planko/status (non-retained)
      if (mqttClient.connect(mqttClientId, TOPIC_STATUS, 0, false, "offline")) {
        Serial.println("CONNECTED! 🎉");
        mqttClient.publish(TOPIC_STATUS, "online", false);
        if (mqttClient.subscribe(mqttCommandTopic, 0)) {
          Serial.printf("📡 [MQTT] Ready! Publishing '%s', '%s', '%s'; listening on '%s'\n",
                        TOPIC_IMU, TOPIC_PAD, TOPIC_PRESSURE,
                        mqttCommandTopic);
        } else {
          Serial.printf("⚠️ [MQTT] Subscribe failed for '%s'\n",
                        mqttCommandTopic);
        }
      } else {
        Serial.printf("failed, rc=%d (will retry in 5s)\n", mqttClient.state());
      }
      lastMqttReconnectAttempt = millis();
    }
  }
}

bool isKnownPad(const char *sensorId) {
  if (sensorId == nullptr || sensorId[0] == '\0')
    return false;
  for (size_t i = 0; i < TOTAL_PADS; ++i) {
    if (strcmp(PAD_CONFIGS[i].name, sensorId) == 0)
      return true;
  }
  return false;
}

void gatewayMqttCallback(char *topic, byte *payload, unsigned int length) {
  if (topic == nullptr || strcmp(topic, mqttCommandTopic) != 0 ||
      payload == nullptr || length == 0)
    return;
  JsonDocument command;
  if (deserializeJson(command, payload, length)) {
    Serial.println("[MQTT] Ignored malformed Gateway command");
    return;
  }

  const char *targetGateway = command["gatewayId"] | "";
  if (strcmp(targetGateway, mqttClientId) != 0)
    return;

  const char *cmd = command["cmd"] | "";
  if (strcmp(cmd, "pad") == 0) {
    const char *pad = command["pad"] | "";
    if (isKnownPad(pad)) {
      snprintf(pendingMqttPadCommand, sizeof(pendingMqttPadCommand), "%s", pad);
      pendingMqttPadEvent = true;
    } else {
      Serial.printf("[MQTT] Ignored unknown pad command: '%s'\n", pad);
    }
  }
}

void sendPadPressEvent(const char *sensorId) {
  char jsonBuffer[128];
  snprintf(jsonBuffer, sizeof(jsonBuffer),
           "{\"sensorId\":\"%s\",\"value\":1,\"gatewayId\":\"%s\"}",
           sensorId, mqttClientId);
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
  JsonDocument doc;
  doc["type"] = "imu_stream";
  doc["schema"] = "planko-imu-v2";
  doc["units"]["accel"] = "m/s^2";
  doc["units"]["gyro"] = "rad/s";
  doc["units"]["orientation"] = "degrees";

  JsonObject gw = doc["gateway"].to<JsonObject>();
  gw["uptimeMs"] = millis();
  gw["connectedNodes"] = connectedCount;
  static uint32_t mqttSequence = 0;
  gw["mqttSeq"] = mqttSequence++;
  gw["clientId"] = mqttClientId;
  gw["bootId"] = gatewayBootId;
  gw["commandTopic"] = mqttCommandTopic;

  JsonObject pressObj = doc["pressure"].to<JsonObject>();
  pressObj["raw"] = rfp602RawValue;
  pressObj["voltage"] = round(rfp602Voltage * 100.0f) / 100.0f;
  pressObj["pressed"] = rfp602Pressed;

  JsonArray nodes = doc["nodes"].to<JsonArray>();
  bool cursorAdvance[MAX_NODES] = {};
  uint16_t cursorSequence[MAX_NODES] = {};
  uint32_t cursorBootId[MAX_NODES] = {};

  for (int i = 0; i < MAX_NODES; i++) {
    const bool nodeConnected =
        nodesTelemetry[i].connected &&
        (nodesTelemetry[i].lastReceivedMillis == 0 ||
         (now - nodesTelemetry[i].lastReceivedMillis) < STALE_TIMEOUT_MS);
    DatasetSample latestSample = {};
    DatasetSample newSamples[MQTT_SAMPLE_BATCH_LIMIT] = {};
    size_t newSampleCount = 0;
    uint32_t nodeBootId = 0;
    bool hasSample = false;

    portENTER_CRITICAL(&datasetMux);
    const NodeDatasetHistory &history = datasetHistory[i];
    if (history.count > 0) {
      const size_t oldest =
          (history.nextIndex + DATASET_HISTORY_CAPACITY - history.count) %
          DATASET_HISTORY_CAPACITY;
      latestSample = history.samples[
          (history.nextIndex + DATASET_HISTORY_CAPACITY - 1) %
          DATASET_HISTORY_CAPACITY];
      nodeBootId = history.bootId;
      hasSample = true;

      const bool sameCursorEpoch =
          mqttPublishedCursorValid[i] &&
          mqttPublishedBootId[i] == nodeBootId;
      uint16_t cursor = sameCursorEpoch ? mqttPublishedSequence[i]
                                        : static_cast<uint16_t>(
                                              history.samples[oldest].sequence - 1);

      for (size_t j = 0;
           j < history.count && newSampleCount < MQTT_SAMPLE_BATCH_LIMIT; ++j) {
        const DatasetSample &candidate =
            history.samples[(oldest + j) % DATASET_HISTORY_CAPACITY];
        const uint16_t delta =
            static_cast<uint16_t>(candidate.sequence - cursor);
        if (delta == 0 || delta >= 0x8000)
          continue;
        newSamples[newSampleCount++] = candidate;
        cursor = candidate.sequence;
      }
      if (newSampleCount > 0) {
        cursorAdvance[i] = true;
        cursorSequence[i] = cursor;
        cursorBootId[i] = nodeBootId;
      }
    }
    portEXIT_CRITICAL(&datasetMux);

    // Flush buffered samples even after a BLE disconnect. Keep connected nodes
    // in the stream for live status; disconnected nodes appear only while a
    // batch is waiting to be published.
    if (!nodeConnected && newSampleCount == 0)
      continue;

    JsonObject n = nodes.add<JsonObject>();
    n["nodeId"] = i + 1;
    n["sensor_uid"] = nodeSensorUids[i];
    n["mount_site"] = nodeConfigs[i].mountSite;
    n["body_part"] = nodeConfigs[i].bodyPart;
    n["side"] = nodeConfigs[i].side;
    n["boot_id"] = nodeBootId;
    n["connected"] = nodeConnected;
    n["seq"] = hasSample ? latestSample.sequence : nodesTelemetry[i].lastSequence;
    n["ts"] = hasSample ? latestSample.timestamp : nodesTelemetry[i].lastTimestamp;
    n["valid"] = hasSample && now - nodesTelemetry[i].lastRawReceivedMillis < 1000;
    n["calibrated"] = hasSample && latestSample.status == 1;
    // IMU bias calibration is not a relative-orientation calibration profile.
    n["relative_orientation_valid"] = false;
    n["pitch"] = round(nodesTelemetry[i].pitch * 10.0f) / 10.0f;
    n["roll"] = round(nodesTelemetry[i].roll * 10.0f) / 10.0f;
    n["yaw"] = round(nodesTelemetry[i].yaw * 10.0f) / 10.0f;
    n["dropped"] = nodesTelemetry[i].totalDropped;
    n["isMock"] = (nodesTelemetry[i].status == 2);

    JsonArray acc = n["accel"].to<JsonArray>();
    acc.add(hasSample ? (latestSample.telemetryValid
                             ? latestSample.ax / 100.0f
                             : latestSample.rawAx / 8192.0f * 9.80665f)
                      : nodesTelemetry[i].ax);
    acc.add(hasSample ? (latestSample.telemetryValid
                             ? latestSample.ay / 100.0f
                             : latestSample.rawAy / 8192.0f * 9.80665f)
                      : nodesTelemetry[i].ay);
    acc.add(hasSample ? (latestSample.telemetryValid
                             ? latestSample.az / 100.0f
                             : latestSample.rawAz / 8192.0f * 9.80665f)
                      : nodesTelemetry[i].az);
    if (hasSample) {
      JsonArray gyro = n["gyro"].to<JsonArray>();
      gyro.add(latestSample.rawGx / 65.5f * 0.01745329252f);
      gyro.add(latestSample.rawGy / 65.5f * 0.01745329252f);
      gyro.add(latestSample.rawGz / 65.5f * 0.01745329252f);
    }

    JsonArray batch = n["samples"].to<JsonArray>();
    for (size_t j = 0; j < newSampleCount; ++j) {
      const DatasetSample &sample = newSamples[j];
      JsonObject row = batch.add<JsonObject>();
      row["boot_id"] = nodeBootId;
      row["seq"] = sample.sequence;
      row["ts"] = sample.timestamp;
      row["valid"] = true;
      row["calibrated"] = sample.status == 1;
      row["isMock"] = false;
      row["relative_orientation_valid"] = false;
      row["pitch"] = sample.telemetryValid ? sample.pitch / 100.0f : 0.0f;
      row["roll"] = sample.telemetryValid ? sample.roll / 100.0f : 0.0f;
      row["yaw"] = sample.telemetryValid ? sample.yaw / 100.0f : 0.0f;

      JsonArray sampleAccel = row["accel"].to<JsonArray>();
      sampleAccel.add(sample.telemetryValid
                          ? sample.ax / 100.0f
                          : sample.rawAx / 8192.0f * 9.80665f);
      sampleAccel.add(sample.telemetryValid
                          ? sample.ay / 100.0f
                          : sample.rawAy / 8192.0f * 9.80665f);
      sampleAccel.add(sample.telemetryValid
                          ? sample.az / 100.0f
                          : sample.rawAz / 8192.0f * 9.80665f);

      JsonArray sampleGyro = row["gyro"].to<JsonArray>();
      sampleGyro.add(sample.rawGx / 65.5f * 0.01745329252f);
      sampleGyro.add(sample.rawGy / 65.5f * 0.01745329252f);
      sampleGyro.add(sample.rawGz / 65.5f * 0.01745329252f);
    }
  }

  String output;
  serializeJson(doc, output);
  bool pubOk = mqttClient.publish(TOPIC_IMU, output.c_str());
  if (pubOk) {
    for (int i = 0; i < MAX_NODES; ++i) {
      if (!cursorAdvance[i])
        continue;
      mqttPublishedSequence[i] = cursorSequence[i];
      mqttPublishedBootId[i] = cursorBootId[i];
      mqttPublishedCursorValid[i] = true;
    }
  }
  static unsigned long lastPubFailLog = 0;
  if (!pubOk && (millis() - lastPubFailLog > 3000)) {
    lastPubFailLog = millis();
    Serial.printf("⚠️ [MQTT] Publish to '%s' failed! rc=%d\n", TOPIC_IMU,
                  mqttClient.state());
  }
}

void readRfp602Sensor() {
  const unsigned long now = millis();
  if (now - lastRfp602SampleTime >= RFP602_SAMPLE_INTERVAL_MS) {
    lastRfp602SampleTime = now;
    rfp602RawValue = analogRead(RFP602_PIN);
    rfp602Voltage = analogReadMilliVolts(RFP602_PIN) / 1000.0f;
    rfp602Pressed = rfp602RawValue >= RFP602_PRESS_THRESHOLD;
  }
  if (mqttClient.connected() && now - lastRfp602MqttTime >= RFP602_MQTT_INTERVAL_MS) {
    lastRfp602MqttTime = now;
    JsonDocument doc;
    doc["raw"] = rfp602RawValue;
    doc["voltage"] = rfp602Voltage;
    doc["pressed"] = rfp602Pressed;
    String output;
    serializeJson(doc, output);
    mqttClient.publish(TOPIC_PRESSURE, output.c_str());
  }
}

void publishBioTelemetry() {
  static unsigned long lastPublish = 0;
  const unsigned long now = millis();
  if (!mqttClient.connected() || !nodesTelemetry[0].connected || now - lastPublish < 250) return;
  lastPublish = now;
  JsonDocument doc;
  doc["nodeId"] = 1;
  doc["bpm"] = nodesTelemetry[0].bpm;
  doc["spo2"] = nodesTelemetry[0].spo2;
  doc["finger"] = nodesTelemetry[0].finger;
  doc["red"] = nodesTelemetry[0].rawRed;
  doc["ir"] = nodesTelemetry[0].rawIr;
  String output;
  serializeJson(doc, output);
  mqttClient.publish(TOPIC_BIO, output.c_str());
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
    Serial.println("Type 'RFP' or 'PRESSURE' to read RFP-602 sensor");
    Serial.println("Type 'STATUS' to see connected BLE nodes & stats");
    return;
  }

  if (input == "RFP" || input == "PRESSURE") {
    Serial.printf("🎯 [RFP-602] Raw ADC: %d | Voltage: %.2fV | State: %s\n",
                  rfp602RawValue, rfp602Voltage,
                  rfp602Pressed ? "PRESSED" : "IDLE");
    return;
  }

  if (input == "STATUS") {
    Serial.println("\n--- [Gateway Telemetry Status] ---");
    for (int i = 0; i < MAX_NODES; i++) {
      Serial.printf(
          "  Node #%d: %s | Seq: %u | Dropped: %u | P:%.1f R:%.1f Y:%.1f\n",
          i + 1, nodesTelemetry[i].connected ? "CONNECTED" : "OFFLINE",
          nodesTelemetry[i].lastSequence,
          static_cast<unsigned>(nodesTelemetry[i].totalDropped),
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
  // Create the namespace on first boot; read-only open reports NOT_FOUND on
  // fresh boards. Keep the compiled defaults if NVS itself is unavailable.
  if (!nodeConfigPreferences.begin("node_cfg", false)) {
    Serial.println("[NODES] NVS unavailable; using default mounting settings");
    return;
  }
  for (int i = 0; i < MAX_NODES; ++i) {
    char key[16];
    snprintf(key, sizeof(key), "name%d", i + 1);
    String name = nodeConfigPreferences.isKey(key)
                      ? nodeConfigPreferences.getString(key, nodeConfigs[i].name)
                      : String(nodeConfigs[i].name);
    if (name.length() > 0)
      name.toCharArray(nodeConfigs[i].name, sizeof(nodeConfigs[i].name));

    snprintf(key, sizeof(key), "site%d", i + 1);
    String site = nodeConfigPreferences.isKey(key)
                      ? nodeConfigPreferences.getString(key, nodeConfigs[i].mountSite)
                      : String(nodeConfigs[i].mountSite);
    if (isAllowedConfigValue(site, sites, sizeof(sites) / sizeof(sites[0])))
      site.toCharArray(nodeConfigs[i].mountSite,
                       sizeof(nodeConfigs[i].mountSite));

    snprintf(key, sizeof(key), "body%d", i + 1);
    String body = nodeConfigPreferences.isKey(key)
                      ? nodeConfigPreferences.getString(key, nodeConfigs[i].bodyPart)
                      : String(nodeConfigs[i].bodyPart);
    if (isAllowedConfigValue(body, bodyParts,
                             sizeof(bodyParts) / sizeof(bodyParts[0])))
      body.toCharArray(nodeConfigs[i].bodyPart,
                       sizeof(nodeConfigs[i].bodyPart));

    snprintf(key, sizeof(key), "side%d", i + 1);
    String side = nodeConfigPreferences.isKey(key)
                      ? nodeConfigPreferences.getString(key, nodeConfigs[i].side)
                      : String(nodeConfigs[i].side);
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
  doc["units"]["accel"] = "m/s^2";
  doc["units"]["gyro"] = "rad/s";
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
      sample["sensor_uid"] = nodeSensorUids[i];
      sample["mount_site"] = nodeConfigs[i].mountSite;
      sample["body_part"] = nodeConfigs[i].bodyPart;
      sample["side"] = nodeConfigs[i].side;
      sample["boot_id"] = snapshotBootId[i];
      sample["accel_unit"] = "m/s^2";
      sample["gyro_unit"] = "rad/s";
      sample["relative_orientation_valid"] = false;
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

  // Initialize RFP-602 Pressure Sensor (ADC1_CH0 on GPIO 0)
  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(RFP602_PIN, INPUT);
  Serial.printf(
      "🎯 [RFP602] Pressure sensor initialized on GPIO %d (ADC1_CH0)\n",
      RFP602_PIN);

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
      // Wi-Fi callbacks can run before NimBLE initialization. Defer BLE work
      // to loop() instead of accessing the scanner from this event task.
      bleScanRestartRequested.store(true);
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
                  WiFi.SSID(i).c_str(), static_cast<int>(WiFi.RSSI(i)),
                  static_cast<int>(WiFi.channel(i)),
                  WiFi.encryptionType(i));
    if (WiFi.SSID(i) == WIFI_SSID) {
      targetChannel = WiFi.channel(i);
    }
  }
  WiFi.scanDelete();

  // Enable PMF (Protected Management Frames) for compatibility with modern
  // Wi-Fi routers
  wifi_config_t current_conf = {};
  if (esp_wifi_get_config(WIFI_IF_STA, &current_conf) == ESP_OK) {
    current_conf.sta.pmf_cfg.capable = true;
    current_conf.sta.pmf_cfg.required = false;
    esp_wifi_set_config(WIFI_IF_STA, &current_conf);
  }

  WiFi.setTxPower(WIFI_POWER_8_5dBm); // Low TX power prevents voltage drop on
                                      // ESP32-C3 DevKit during auth
  Serial.println("🌐 [WIFI] DHCP enabled: requesting IP, gateway and DNS from Wi-Fi AP");
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
  Serial.printf("[WIFI] Gateway: %s | Subnet: %s | DNS1: %s | DNS2: %s\n",
                WiFi.gatewayIP().toString().c_str(),
                WiFi.subnetMask().toString().c_str(),
                WiFi.dnsIP(0).toString().c_str(),
                WiFi.dnsIP(1).toString().c_str());
  setupDatasetServer();

  // Initialize MQTT Client
  snprintf(mqttClientId, sizeof(mqttClientId), "PlanKO_GW_%08X",
           static_cast<unsigned>(ESP.getEfuseMac() & 0xFFFFFFFFULL));
  snprintf(mqttCommandTopic, sizeof(mqttCommandTopic),
           "planko/gateway/%s/cmd", mqttClientId);
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(gatewayMqttCallback);
  mqttClient.setBufferSize(4096); // Four nodes with six axes and stream metadata.
  mqttClient.setKeepAlive(30);
  mqttClient.setSocketTimeout(5);
  Serial.printf("🔌 MQTT Broker: %s:%u (ClientID: %s)\n", MQTT_BROKER,
                MQTT_PORT, mqttClientId);
  maintainMqttConnection();
  Serial.printf("💾 [MEMORY] Free Heap: %u bytes\n",
                static_cast<unsigned>(ESP.getFreeHeap()));

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
  Serial.printf("🎉 [INIT] Setup completed! Scanner active, MQTT %s.\n",
                mqttClient.connected() ? "connected" : "waiting for broker");
  Serial.flush();
}

void loop() {
  if (bleScanRestartRequested.exchange(false))
    resumeBleScanIfNeeded(true);
  maintainMqttConnection();
  if (mqttClient.connected()) {
    mqttClient.loop();
  }
  if (pendingMqttPadEvent) {
    pendingMqttPadEvent = false;
    sendPadPressEvent(pendingMqttPadCommand);
  }
  webServer.handleClient();
  readRfp602Sensor();
  handlePadInputs();
  handleSerialSimulator();

  unsigned long now = millis();

  // Check for pending BLE connections (one at a time to keep MQTT & Wi-Fi
  // responsive)
  static int nextBleNode = 0;
  for (int offset = 0; offset < MAX_NODES; offset++) {
    const int i = (nextBleNode + offset) % MAX_NODES;
    portENTER_CRITICAL(&bleConnectMux);
    const bool pending = pendingBleNodes[i].pending;
    portEXIT_CRITICAL(&bleConnectMux);
    if (pending) {
      connectToNode(i);
      nextBleNode = (i + 1) % MAX_NODES;
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

  // Keep 50 ms cadence without catch-up bursts after blocking work.
  const unsigned long publishNow = millis();
  if (publishNow - lastMqttStreamTime >= MQTT_STREAM_INTERVAL_MS) {
    lastMqttStreamTime += ((publishNow - lastMqttStreamTime) / MQTT_STREAM_INTERVAL_MS) * MQTT_STREAM_INTERVAL_MS;
    publishImuStream();
  }

  // Periodic MQTT Publish of Node 1 biometric telemetry (4Hz)
  publishBioTelemetry();

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
    const int connectedCount = connectedExpectedBleNodeCount();
    char nodesSummary[64] = "";
    for (size_t expectedIndex = 0; expectedIndex < EXPECTED_NODE_COUNT;
         ++expectedIndex) {
      const int i = EXPECTED_NODE_IDS[expectedIndex] - 1;
      if (nodesTelemetry[i].connected) {
        char tmp[16];
        snprintf(tmp, sizeof(tmp), "#%d ", i + 1);
        strcat(nodesSummary, tmp);
      }
    }
    if (connectedCount == 0)
      strcpy(nodesSummary, "None");

    Serial.printf("🌐 [GATEWAY] Wi-Fi: %s (%s) | MQTT: %s | BLE: %d/%u [%s] | "
                  "RFP: %d (%s) | Heap: %uB | Uptime: %lus\n",
                  (WiFi.status() == WL_CONNECTED) ? "ONLINE" : "OFFLINE",
                  (WiFi.status() == WL_CONNECTED)
                      ? WiFi.localIP().toString().c_str()
                      : "No IP",
                  mqttClient.connected() ? "CONNECTED" : "DISCONNECTED",
                  connectedCount, static_cast<unsigned>(EXPECTED_NODE_COUNT),
                  nodesSummary, rfp602RawValue,
                  rfp602Pressed ? "PRESSED" : "RELEASED",
                  static_cast<unsigned>(ESP.getFreeHeap()), now / 1000);
  }
}

#else
#error                                                                         \
    "Please select an environment: 'gateway' or 'imu_node_1'..'imu_node_4' in platformio.ini"
#endif
