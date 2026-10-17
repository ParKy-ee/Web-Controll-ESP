#ifndef MPU6050_READER_H
#define MPU6050_READER_H

#include <Arduino.h>
#include <Wire.h>
#include <math.h>

class Mpu6050Reader {
public:
    float pitch = 0.0f;
    float roll = 0.0f;
    float yaw = 0.0f;

    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;

    float gx = 0.0f;
    float gy = 0.0f;
    float gz = 0.0f;

    int16_t rawAx = 0;
    int16_t rawAy = 0;
    int16_t rawAz = 0;
    int16_t rawGx = 0;
    int16_t rawGy = 0;
    int16_t rawGz = 0;

    bool isDetected = false;
    bool isCalibrated = false;

    // Routine to unwedge I2C bus if a slave hung holding SDA LOW
    static void recoverI2CBus(int sdaPin, int sclPin) {
        pinMode(sdaPin, INPUT_PULLUP);
        pinMode(sclPin, OUTPUT);
        // Pulse SCL 9 times to allow slave device to finish byte transmission
        for (int i = 0; i < 9; i++) {
            digitalWrite(sclPin, LOW);
            delayMicroseconds(5);
            digitalWrite(sclPin, HIGH);
            delayMicroseconds(5);
        }
        // Send STOP condition
        pinMode(sdaPin, OUTPUT);
        digitalWrite(sdaPin, LOW);
        delayMicroseconds(5);
        digitalWrite(sclPin, HIGH);
        delayMicroseconds(5);
        digitalWrite(sdaPin, HIGH);
        delayMicroseconds(5);
        pinMode(sdaPin, INPUT_PULLUP);
        pinMode(sclPin, INPUT_PULLUP);
    }

    bool begin(int sdaPin = 8, int sclPin = 9) {
        _sdaPin = sdaPin;
        _sclPin = sclPin;

        recoverI2CBus(_sdaPin, _sclPin);

        Wire.end();
        delay(10);
        // Use 100kHz Standard Mode for rock-solid signal integrity over jumper wires
        Wire.begin(_sdaPin, _sclPin, 100000);
        Wire.setTimeOut(100);
        delay(40);

        // Auto-detect I2C address: 0x68 (AD0=GND) or 0x69 (AD0=3.3V)
        uint8_t foundAddr = 0;
        Wire.beginTransmission(0x68);
        if (Wire.endTransmission() == 0) {
            foundAddr = 0x68;
        } else {
            Wire.beginTransmission(0x69);
            if (Wire.endTransmission() == 0) {
                foundAddr = 0x69;
            }
        }

        if (foundAddr == 0) {
            isDetected = false;
            return false;
        }
        _mpuAddr = foundAddr;

        // Check MPU-6050 WhoAmI register (0x75)
        Wire.beginTransmission(_mpuAddr);
        Wire.write(0x75);
        if (Wire.endTransmission(false) != 0) {
            isDetected = false;
            return false;
        }

        if (Wire.requestFrom((uint8_t)_mpuAddr, (uint8_t)1) > 0) {
            uint8_t whoAmI = Wire.read();
            Serial.printf("🔍 [MPU6050] Found at 0x%02X, WHO_AM_I = 0x%02X\n", _mpuAddr, whoAmI);
        }

        // 1. Reset MPU-6050 (write 0x80 to PWR_MGMT_1, reg 0x6B)
        writeRegister(0x6B, 0x80);
        delay(60);

        // 2. Wake up MPU-6050 and set clock source to PLL with X Gyro (reg 0x6B = 0x01)
        writeRegister(0x6B, 0x01);
        delay(30);

        // 3. Set DLPF (Digital Low Pass Filter) to ~42Hz (reg 0x1A = 0x03)
        writeRegister(0x1A, 0x03);

        // 4. Configure Gyro Full Scale to +/- 500 deg/s (FS_SEL = 1, reg 0x1B = 0x08)
        writeRegister(0x1B, 0x08);

        // 5. Configure Accel Full Scale to +/- 4g (AFS_SEL = 1, reg 0x1C = 0x08)
        writeRegister(0x1C, 0x08);
        delay(20);

        isDetected = true;
        consecutiveErrors = 0;
        calibrate(80);
        lastFilterMicros = micros();
        return true;
    }

    void calibrate(int samples = 80) {
        if (!isDetected) return;

        Serial.println("⚖️ [MPU6050] Calibrating IMU sensor (Keep still on flat surface)...");
        long gxSum = 0, gySum = 0, gzSum = 0;
        long axSum = 0, aySum = 0, azSum = 0;
        int validSamples = 0;

        for (int i = 0; i < samples; i++) {
            int16_t rAx, rAy, rAz, rTemp, rGx, rGy, rGz;
            if (readRawData(rAx, rAy, rAz, rTemp, rGx, rGy, rGz)) {
                axSum += rAx;
                aySum += rAy;
                azSum += rAz;
                gxSum += rGx;
                gySum += rGy;
                gzSum += rGz;
                validSamples++;
            }
            delay(10);
        }

        if (validSamples > 20) {
            gxOffset = (float)gxSum / (validSamples * 65.5f);
            gyOffset = (float)gySum / (validSamples * 65.5f);
            gzOffset = (float)gzSum / (validSamples * 65.5f);

            axOffset = ((float)axSum / validSamples) / 8192.0f;
            ayOffset = ((float)aySum / validSamples) / 8192.0f;
            azOffset = (((float)azSum / validSamples) / 8192.0f) - 1.0f;

            isCalibrated = true;
            Serial.printf("✅ [MPU6050] Calibrated (%d samples): GyroBias[%.2f, %.2f, %.2f] deg/s | AccelBias[%.2f, %.2f, %.2f] g\n",
                          validSamples, gxOffset, gyOffset, gzOffset, axOffset, ayOffset, azOffset);
        } else {
            isCalibrated = false;
            Serial.printf("⚠️ [MPU6050] Calibration incomplete (only %d valid samples). Offsets cleared.\n", validSamples);
        }
    }

    bool update() {
        unsigned long now = millis();

        // If sensor was not detected, retry periodically every 2 seconds
        if (!isDetected) {
            if (now - lastRetryMillis >= 2000) {
                lastRetryMillis = now;
                // Quick address probe
                bool detected = false;
                Wire.beginTransmission(0x68);
                if (Wire.endTransmission() == 0) detected = true;
                if (!detected) {
                    Wire.beginTransmission(0x69);
                    if (Wire.endTransmission() == 0) detected = true;
                }
                if (detected) {
                    Serial.println("🎉 [MPU6050] Sensor detected! Initializing...");
                    begin(_sdaPin, _sclPin);
                }
            }
            return false;
        }

        int16_t rAx, rAy, rAz, rTemp, rGx, rGy, rGz;
        if (!readRawData(rAx, rAy, rAz, rTemp, rGx, rGy, rGz)) {
            return false;
        }

        this->rawAx = rAx;
        this->rawAy = rAy;
        this->rawAz = rAz;
        this->rawGx = rGx;
        this->rawGy = rGy;
        this->rawGz = rGz;

        // Convert raw accel to m/s^2 (AFS_SEL=1 -> 8192 LSB/g, 1g = 9.80665 m/s^2)
        ax = ((float)rAx / 8192.0f - axOffset) * 9.80665f;
        ay = ((float)rAy / 8192.0f - ayOffset) * 9.80665f;
        az = ((float)rAz / 8192.0f - azOffset) * 9.80665f;

        // Convert raw gyro to deg/s (FS_SEL=1 -> 65.5 LSB/(deg/s))
        gx = ((float)rGx / 65.5f) - gxOffset;
        gy = ((float)rGy / 65.5f) - gyOffset;
        gz = ((float)rGz / 65.5f) - gzOffset;

        // Compute delta time
        unsigned long currentMicros = micros();
        float dt = (currentMicros - lastFilterMicros) / 1000000.0f;
        if (dt <= 0.0f || dt > 0.5f) dt = 0.05f; // 20Hz (50ms interval)
        lastFilterMicros = currentMicros;

        // Calculate pitch and roll from accelerometer (in degrees)
        float accPitch = atan2(-ax, sqrt(ay * ay + az * az)) * 180.0f / M_PI;
        float accRoll  = atan2(ay, az) * 180.0f / M_PI;

        // Complementary Filter: 96% Gyroscope integration + 4% Accelerometer reference
        pitch = 0.96f * (pitch + gx * dt) + 0.04f * accPitch;
        roll  = 0.96f * (roll + gy * dt) + 0.04f * accRoll;
        yaw  += gz * dt;

        if (yaw > 180.0f) yaw -= 360.0f;
        if (yaw < -180.0f) yaw += 360.0f;

        return true;
    }

private:
    uint8_t _mpuAddr = 0x68;
    int _sdaPin = 8;
    int _sclPin = 9;
    uint8_t consecutiveErrors = 0;
    static const uint8_t MAX_CONSECUTIVE_ERRORS = 15;

    float gxOffset = 0, gyOffset = 0, gzOffset = 0;
    float axOffset = 0, ayOffset = 0, azOffset = 0;
    unsigned long lastFilterMicros = 0;
    unsigned long lastRetryMillis = 0;

    bool writeRegister(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(_mpuAddr);
        Wire.write(reg);
        Wire.write(val);
        return (Wire.endTransmission() == 0);
    }

    bool readRawData(int16_t &rawAx, int16_t &rawAy, int16_t &rawAz,
                     int16_t &rawTemp, int16_t &rawGx, int16_t &rawGy, int16_t &rawGz) {
        Wire.beginTransmission(_mpuAddr);
        Wire.write(0x3B);
        if (Wire.endTransmission(false) != 0) {
            consecutiveErrors++;
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                isDetected = false;
                Serial.printf("❌ [MPU6050] Bus write error (streak: %d)\n", consecutiveErrors);
            }
            return false;
        }

        if (Wire.requestFrom((uint8_t)_mpuAddr, (uint8_t)14) < 14) {
            consecutiveErrors++;
            if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS) {
                isDetected = false;
                Serial.printf("❌ [MPU6050] Bus read error (streak: %d)\n", consecutiveErrors);
            }
            return false;
        }

        consecutiveErrors = 0;
        rawAx   = (Wire.read() << 8) | Wire.read();
        rawAy   = (Wire.read() << 8) | Wire.read();
        rawAz   = (Wire.read() << 8) | Wire.read();
        rawTemp = (Wire.read() << 8) | Wire.read();
        rawGx   = (Wire.read() << 8) | Wire.read();
        rawGy   = (Wire.read() << 8) | Wire.read();
        rawGz   = (Wire.read() << 8) | Wire.read();
        return true;
    }
};

#endif // MPU6050_READER_H
