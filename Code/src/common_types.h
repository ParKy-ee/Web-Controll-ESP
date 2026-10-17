#ifndef COMMON_TYPES_H
#define COMMON_TYPES_H

#include <Arduino.h>

// BLE Service & Characteristic UUIDs
#define PLANKO_SERVICE_UUID        "4E6F6465-0001-1000-8000-00805F9B34FB"
#define PLANKO_CHARACTERISTIC_UUID "4E6F6465-0002-1000-8000-00805F9B34FB"

// Compact telemetry packet retained for the existing MQTT stream.
// It fits the default 23-byte ATT MTU without negotiation.
struct __attribute__((packed)) ImuPacket {
    uint8_t  nodeId;       // Node ID (1 - 4)
    uint16_t sequence;     // Packet Sequence number (for packet loss tracking)
    uint32_t timestamp;    // millis() on node
    int16_t  pitch;        // Pitch in degrees * 100
    int16_t  roll;         // Roll in degrees * 100
    int16_t  yaw;          // Yaw in degrees * 100
    int16_t  ax;           // Accel X (m/s^2 * 100)
    int16_t  ay;           // Accel Y (m/s^2 * 100)
    int16_t  az;           // Accel Z (m/s^2 * 100)
    uint8_t  status;       // 1 = Calibrated/OK, 0 = Uncalibrated/Error
};

// The Gateway also needs the real six-axis readings for Dataset capture.
// This second notification uses the same 20-byte payload size and is tagged
// so old telemetry fields and raw sensor counts are never confused.
#define RAW_IMU_PACKET_MARKER 0xA4
#define RAW_IMU_PACKET_MARKER_MASK 0xFE
struct __attribute__((packed)) RawImuPacket {
    uint8_t  nodeId;
    uint16_t sequence;
    uint32_t timestamp;
    int16_t  ax;            // MPU-6050 raw accelerometer register value
    int16_t  ay;
    int16_t  az;
    int16_t  gx;            // MPU-6050 raw gyroscope register value
    int16_t  gy;
    int16_t  gz;
    uint8_t  marker;        // Marker high bits; low bit carries calibrated status
};

// Biomarker packet (Heart Rate, SpO2, Red/IR PPG) for MAX30102 on Node 1
#define BIO_PACKET_MARKER 0xB4
#define BIO_PACKET_MARKER_MASK 0xFE
struct __attribute__((packed)) BioPacket {
    uint8_t  nodeId;       // Node ID (1)
    uint16_t sequence;     // Packet Sequence number
    uint32_t timestamp;    // millis() on node
    uint16_t bpm;          // Heart rate (BPM * 10, e.g. 720 = 72.0 BPM)
    uint8_t  spo2;         // SpO2 percentage (e.g. 98 = 98%)
    uint8_t  finger;       // 1 = finger detected, 0 = no finger
    uint32_t red;          // Raw Red LED reading
    uint32_t ir;           // Raw IR LED reading
    uint8_t  marker;       // BIO_PACKET_MARKER (0xB4)
};

static_assert(sizeof(ImuPacket) == 20, "ImuPacket must remain 20 bytes");
static_assert(sizeof(RawImuPacket) == 20, "RawImuPacket must remain 20 bytes");
static_assert(sizeof(BioPacket) == 20, "BioPacket must remain 20 bytes");

// Telemetry state stored on Gateway for each node
struct NodeTelemetry {
    volatile bool connected; // volatile: written by BLE callback task, read by main loop
    uint16_t lastSequence;
    uint32_t totalDropped;
    uint32_t lastTimestamp;
    float    pitch;
    float    roll;
    float    yaw;
    float    ax;
    float    ay;
    float    az;
    uint8_t  status;
    float    bpm;
    uint8_t  spo2;
    bool     finger;
    uint32_t rawRed;
    uint32_t rawIr;
    volatile unsigned long lastReceivedMillis; // volatile: written by BLE callback task, read by main loop
};

#endif // COMMON_TYPES_H
