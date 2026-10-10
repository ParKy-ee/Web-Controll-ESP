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

    bool isDetected = false;
    bool isCalibrated = false;

    bool begin(int sdaPin = 4, int sclPin = 5) {
        _sdaPin = sdaPin;
        _sclPin = sclPin;

        pinMode(_sdaPin, INPUT_PULLUP);
        pinMode(_sclPin, INPUT_PULLUP);
        Wire.begin(_sdaPin, _sclPin, 400000);
        Wire.setTimeOut(50);
        delay(50);

        // Ping MPU-6050 at address 0x68
        Wire.beginTransmission(MPU_ADDR);
        if (Wire.endTransmission() != 0) {
            isDetected = false;
            return false;
        }

        // Check MPU-6050 WhoAmI register (0x75)
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(0x75);
        if (Wire.endTransmission(false) != 0) {
            isDetected = false;
            return false;
        }

        if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)1) > 0) {
            uint8_t whoAmI = Wire.read();
            if (whoAmI != 0x68 && whoAmI != 0x70 && whoAmI != 0x72 && whoAmI != 0x98) {
                Serial.printf("⚠️ [MPU6050] Unexpected WHO_AM_I: 0x%02X\n", whoAmI);
            }
        }

        // 1. Wake up MPU-6050 (write 0x00 to PWR_MGMT_1, register 0x6B)
        writeRegister(0x6B, 0x00);
        delay(20);

        // 2. Set DLPF (Digital Low Pass Filter) to ~42Hz (register 0x1A = 0x03)
        writeRegister(0x1A, 0x03);

        // 3. Configure Gyro Full Scale to +/- 500 deg/s (FS_SEL = 1, register 0x1B = 0x08)
        writeRegister(0x1B, 0x08);

        // 4. Configure Accel Full Scale to +/- 4g (AFS_SEL = 1, register 0x1C = 0x08)
        writeRegister(0x1C, 0x08);
        delay(20);

        isDetected = true;
        calibrate();
        lastFilterMicros = micros();
        return true;
    }

    void calibrate(int samples = 100) {
        if (!isDetected) return;

        Serial.println("⚖️ [MPU6050] Calibrating IMU sensor (Keep still on flat surface)...");
        long gxSum = 0, gySum = 0, gzSum = 0;
        long axSum = 0, aySum = 0, azSum = 0;
        int validSamples = 0;

        for (int i = 0; i < samples; i++) {
            int16_t rawAx, rawAy, rawAz, rawTemp, rawGx, rawGy, rawGz;
            if (readRawData(rawAx, rawAy, rawAz, rawTemp, rawGx, rawGy, rawGz)) {
                axSum += rawAx;
                aySum += rawAy;
                azSum += rawAz;
                gxSum += rawGx;
                gySum += rawGy;
                gzSum += rawGz;
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
            Serial.printf("✅ [MPU6050] Calibrated: GyroBias[%.2f, %.2f, %.2f] deg/s | AccelBias[%.2f, %.2f, %.2f] g\n",
                          gxOffset, gyOffset, gzOffset, axOffset, ayOffset, azOffset);
        } else {
            isCalibrated = false;
        }
    }

    bool update() {
        unsigned long now = millis();

        // If sensor was not detected, try reconnecting periodically without spamming I2C
        if (!isDetected) {
            if (now - lastRetryMillis >= 10000) {
                lastRetryMillis = now;
                // Quick ping without spam
                Wire.beginTransmission(MPU_ADDR);
                if (Wire.endTransmission() == 0) {
                    Serial.println("🎉 [MPU6050] Sensor detected! Initializing...");
                    begin(_sdaPin, _sclPin);
                }
            }
            return false;
        }

        int16_t rawAx, rawAy, rawAz, rawTemp, rawGx, rawGy, rawGz;
        if (!readRawData(rawAx, rawAy, rawAz, rawTemp, rawGx, rawGy, rawGz)) {
            return false;
        }

        // Convert raw accel to m/s^2 (AFS_SEL=1 -> 8192 LSB/g, 1g = 9.80665 m/s^2)
        ax = ((float)rawAx / 8192.0f - axOffset) * 9.80665f;
        ay = ((float)rawAy / 8192.0f - ayOffset) * 9.80665f;
        az = ((float)rawAz / 8192.0f - azOffset) * 9.80665f;

        // Convert raw gyro to deg/s (FS_SEL=1 -> 65.5 LSB/(deg/s))
        gx = ((float)rawGx / 65.5f) - gxOffset;
        gy = ((float)rawGy / 65.5f) - gyOffset;
        gz = ((float)rawGz / 65.5f) - gzOffset;

        // Compute delta time
        unsigned long currentMicros = micros();
        float dt = (currentMicros - lastFilterMicros) / 1000000.0f;
        if (dt <= 0.0f || dt > 0.5f) dt = 0.02f;
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
    const uint8_t MPU_ADDR = 0x68;
    int _sdaPin = 4;
    int _sclPin = 5;
    float gxOffset = 0, gyOffset = 0, gzOffset = 0;
    float axOffset = 0, ayOffset = 0, azOffset = 0;
    unsigned long lastFilterMicros = 0;
    unsigned long lastRetryMillis = 0;

    void writeRegister(uint8_t reg, uint8_t val) {
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(reg);
        Wire.write(val);
        Wire.endTransmission();
    }

    bool readRawData(int16_t &rawAx, int16_t &rawAy, int16_t &rawAz,
                     int16_t &rawTemp, int16_t &rawGx, int16_t &rawGy, int16_t &rawGz) {
        Wire.beginTransmission(MPU_ADDR);
        Wire.write(0x3B);
        if (Wire.endTransmission(false) != 0) {
            isDetected = false;
            return false;
        }

        if (Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)14) < 14) {
            isDetected = false;
            return false;
        }

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
