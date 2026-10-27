#include <Arduino.h>
#include "rfp605_sensor.h"
#if defined(RFP605_GATEWAY_INTEGRATION)
#include <atomic>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <freertos/queue.h>
#endif

#if defined(RFP605_SANDBOX) || defined(RFP605_GATEWAY_INTEGRATION)

#ifndef RFP605_ADC_PIN
#define RFP605_ADC_PIN 32
#endif
#ifndef RFP605_MUX_S0_PIN
#define RFP605_MUX_S0_PIN 16
#endif
#ifndef RFP605_MUX_S1_PIN
#define RFP605_MUX_S1_PIN 17
#endif
#ifndef RFP605_MUX_S2_PIN
#define RFP605_MUX_S2_PIN 18
#endif
#ifndef RFP605_MUX_S3_PIN
#define RFP605_MUX_S3_PIN 19
#endif
// Bit N enables mux channel Cn. Keep all bits enabled when all 16 channels
// have their own sensor and pull-down resistor wired.
#ifndef RFP605_ACTIVE_MASK
#define RFP605_ACTIVE_MASK 0xF801
#endif
#ifndef RFP605_CALIBRATION_SAMPLES
#define RFP605_CALIBRATION_SAMPLES 20
#endif
#ifndef RFP605_PRESS_DELTA_RAW
#define RFP605_PRESS_DELTA_RAW 100
#endif
#ifndef RFP605_RELEASE_DELTA_RAW
#define RFP605_RELEASE_DELTA_RAW 50
#endif
#ifndef RFP605_MAX_PRESS_DELTA_RAW
#define RFP605_MAX_PRESS_DELTA_RAW 300
#endif
#ifndef RFP605_MAX_RELEASE_DELTA_RAW
#define RFP605_MAX_RELEASE_DELTA_RAW 150
#endif
#ifndef RFP605_DEBUG_SCAN
#define RFP605_DEBUG_SCAN 0
#endif

static constexpr uint8_t ADC_PIN = RFP605_ADC_PIN;
static constexpr uint8_t MUX_S0_PIN = RFP605_MUX_S0_PIN;
static constexpr uint8_t MUX_S1_PIN = RFP605_MUX_S1_PIN;
static constexpr uint8_t MUX_S2_PIN = RFP605_MUX_S2_PIN;
static constexpr uint8_t MUX_S3_PIN = RFP605_MUX_S3_PIN;
static constexpr uint16_t ACTIVE_CHANNEL_MASK =
    static_cast<uint16_t>(RFP605_ACTIVE_MASK);

static constexpr uint8_t CHANNEL_COUNT = 16;
static constexpr uint32_t SCAN_INTERVAL_MS = 50; // 20 Hz target per channel
static constexpr uint8_t ADC_AVERAGE_COUNT = 4;
static constexpr uint8_t ADC_DISCARD_COUNT = 4;
static constexpr uint8_t PRESS_CONFIRM_SAMPLES = 2;
static constexpr uint8_t RELEASE_CONFIRM_SAMPLES = 2;
static constexpr uint16_t MUX_SETTLE_US = 1000;

static const char *const SENSOR_IDS[CHANNEL_COUNT] = {
    "RFP605_C00", "RFP605_C01", "RFP605_C02", "RFP605_C03",
    "RFP605_C04", "RFP605_C05", "RFP605_C06", "RFP605_C07",
    "RFP605_C08", "RFP605_C09", "RFP605_C10", "RFP605_C11",
    "RFP605_C12", "RFP605_C13", "RFP605_C14", "RFP605_C15"};

struct ChannelState {
  uint32_t baselineSum = 0;
  uint32_t sequence = 0;
  uint16_t baselineRaw = 0;
  uint16_t pressDeltaRaw = 0;
  uint16_t releaseDeltaRaw = 0;
  uint16_t calibrationMinRaw = 4095;
  uint16_t calibrationMaxRaw = 0;
  uint8_t pressCount = 0;
  uint8_t releaseCount = 0;
  bool pressed = false;
};

static ChannelState channels[CHANNEL_COUNT];
static uint32_t lastScanMs = 0;
#if defined(RFP605_GATEWAY_INTEGRATION)
static constexpr UBaseType_t RFP605_QUEUE_CAPACITY = 256;
static QueueHandle_t rfp605SampleQueue = nullptr;
static TaskHandle_t rfp605TaskHandle = nullptr;
static portMUX_TYPE rfp605StateMux = portMUX_INITIALIZER_UNLOCKED;
static std::atomic<uint32_t> rfp605QueueDropCount{0};
#endif

static void selectMuxChannel(uint8_t channel) {
  digitalWrite(MUX_S0_PIN, (channel & 0x01) ? HIGH : LOW);
  digitalWrite(MUX_S1_PIN, (channel & 0x02) ? HIGH : LOW);
  digitalWrite(MUX_S2_PIN, (channel & 0x04) ? HIGH : LOW);
  digitalWrite(MUX_S3_PIN, (channel & 0x08) ? HIGH : LOW);
}

static uint16_t readRawAverage(uint8_t channel) {
  selectMuxChannel(channel);
  delayMicroseconds(MUX_SETTLE_US);

  // Let the mux/ADC settle, then flush conversions left from the prior channel.
  for (uint8_t i = 0; i < ADC_DISCARD_COUNT; ++i)
    (void)analogRead(ADC_PIN);

  uint32_t total = 0;
  for (uint8_t i = 0; i < ADC_AVERAGE_COUNT; ++i)
    total += static_cast<uint16_t>(analogRead(ADC_PIN));

  return static_cast<uint16_t>(total / ADC_AVERAGE_COUNT);
}

static uint16_t readMillivoltsAverage() {
  uint32_t total = 0;
  for (uint8_t i = 0; i < ADC_AVERAGE_COUNT; ++i)
    total += static_cast<uint16_t>(analogReadMilliVolts(ADC_PIN));

  return static_cast<uint16_t>(total / ADC_AVERAGE_COUNT);
}

static void calibrateReleasedBaseline() {
  Serial.printf(
      "{\"type\":\"calibrating\",\"samples_per_channel\":%u,"
      "\"duration_ms\":%lu,\"instruction\":\"leave_sensors_released\"}\n",
      static_cast<unsigned>(RFP605_CALIBRATION_SAMPLES),
      static_cast<unsigned long>(RFP605_CALIBRATION_SAMPLES * SCAN_INTERVAL_MS));

  for (uint8_t sample = 0; sample < RFP605_CALIBRATION_SAMPLES; ++sample) {
    for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
      if ((ACTIVE_CHANNEL_MASK & (1U << channel)) == 0)
        continue;

      const uint16_t raw = readRawAverage(channel);
      channels[channel].baselineSum += raw;
      if (raw < channels[channel].calibrationMinRaw)
        channels[channel].calibrationMinRaw = raw;
      if (raw > channels[channel].calibrationMaxRaw)
        channels[channel].calibrationMaxRaw = raw;
    }
    delay(SCAN_INTERVAL_MS);
  }

  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    if ((ACTIVE_CHANNEL_MASK & (1U << channel)) == 0)
      continue;

    ChannelState &state = channels[channel];
    state.baselineRaw = static_cast<uint16_t>(
        state.baselineSum / RFP605_CALIBRATION_SAMPLES);

    // Raise thresholds above observed idle noise, while keeping a configurable
    // minimum response for sensors with a very quiet baseline.
    const uint16_t noiseRange =
        state.calibrationMaxRaw - state.calibrationMinRaw;
    const uint16_t adaptivePressDelta = static_cast<uint16_t>(noiseRange * 2U);
    const uint16_t adaptiveReleaseDelta = noiseRange;
    uint16_t pressDelta =
        adaptivePressDelta > RFP605_PRESS_DELTA_RAW
            ? adaptivePressDelta
            : RFP605_PRESS_DELTA_RAW;
    uint16_t releaseDelta =
        adaptiveReleaseDelta > RFP605_RELEASE_DELTA_RAW
            ? adaptiveReleaseDelta
            : RFP605_RELEASE_DELTA_RAW;

    // A single calibration spike must not make a channel impossible to press.
    if (pressDelta > RFP605_MAX_PRESS_DELTA_RAW)
      pressDelta = RFP605_MAX_PRESS_DELTA_RAW;
    if (releaseDelta > RFP605_MAX_RELEASE_DELTA_RAW)
      releaseDelta = RFP605_MAX_RELEASE_DELTA_RAW;
    if (releaseDelta >= pressDelta)
      releaseDelta = pressDelta / 2U;

    state.pressDeltaRaw = pressDelta;
    state.releaseDeltaRaw = releaseDelta;
  }

  Serial.printf(
      "{\"type\":\"ready\",\"mux\":\"CD74HC4067\","
      "\"adc_gpio\":%u,\"active_mask\":\"0x%04X\","
      "\"scan_interval_ms\":50,\"debug_scan\":%u}\n",
      static_cast<unsigned>(ADC_PIN),
      static_cast<unsigned>(ACTIVE_CHANNEL_MASK),
      static_cast<unsigned>(RFP605_DEBUG_SCAN));
}

static bool updatePressedState(ChannelState &state, uint16_t raw) {
  const uint16_t deviation = raw >= state.baselineRaw
                                 ? raw - state.baselineRaw
                                 : state.baselineRaw - raw;

  if (!state.pressed) {
    if (deviation >= state.pressDeltaRaw) {
      if (state.pressCount < PRESS_CONFIRM_SAMPLES)
        ++state.pressCount;
    } else {
      state.pressCount = 0;
    }

    if (state.pressCount >= PRESS_CONFIRM_SAMPLES) {
      state.pressed = true;
      state.pressCount = 0;
      state.releaseCount = 0;
    }
  } else {
    if (deviation <= state.releaseDeltaRaw) {
      if (state.releaseCount < RELEASE_CONFIRM_SAMPLES)
        ++state.releaseCount;
    } else {
      state.releaseCount = 0;
    }

    if (state.releaseCount >= RELEASE_CONFIRM_SAMPLES) {
      state.pressed = false;
      state.releaseCount = 0;
      state.pressCount = 0;
    }
  }

  return state.pressed && deviation > state.releaseDeltaRaw;
}

void rfp605Begin() {
  pinMode(MUX_S0_PIN, OUTPUT);
  pinMode(MUX_S1_PIN, OUTPUT);
  pinMode(MUX_S2_PIN, OUTPUT);
  pinMode(MUX_S3_PIN, OUTPUT);
  selectMuxChannel(0);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);
  pinMode(ADC_PIN, INPUT);

  calibrateReleasedBaseline();
}

#if defined(RFP605_GATEWAY_INTEGRATION)
static void rfp605SamplerTask(void *parameter) {
  (void)parameter;
  Rfp605Sample batch[CHANNEL_COUNT] = {};
  for (;;) {
    const size_t count = rfp605Update(batch, CHANNEL_COUNT);
    for (size_t i = 0; i < count; ++i) {
      if (xQueueSend(rfp605SampleQueue, &batch[i], 0) != pdTRUE) {
        // Keep the newest pressure readings if the gateway loop is busy.
        Rfp605Sample discarded = {};
        if (xQueueReceive(rfp605SampleQueue, &discarded, 0) == pdTRUE) {
          rfp605QueueDropCount.fetch_add(1, std::memory_order_relaxed);
          (void)xQueueSend(rfp605SampleQueue, &batch[i], 0);
        }
      }
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

void rfp605Start() {
  if (rfp605TaskHandle != nullptr)
    return;
  rfp605SampleQueue = xQueueCreate(RFP605_QUEUE_CAPACITY,
                                   sizeof(Rfp605Sample));
  if (rfp605SampleQueue == nullptr) {
    Serial.println("[RFP605] Failed to create sample queue");
    return;
  }
  const BaseType_t result = xTaskCreatePinnedToCore(
      rfp605SamplerTask, "rfp605_scan", 4096, nullptr, 1,
      &rfp605TaskHandle, 1);
  if (result != pdPASS) {
    rfp605TaskHandle = nullptr;
    vQueueDelete(rfp605SampleQueue);
    rfp605SampleQueue = nullptr;
    Serial.println("[RFP605] Failed to start scanner task");
    return;
  }
  Serial.printf("[RFP605] Background scan active at 20 Hz/channel; queue=%u\n",
                static_cast<unsigned>(RFP605_QUEUE_CAPACITY));
}

size_t rfp605ReadQueuedSamples(Rfp605Sample *samples, size_t capacity) {
  if (rfp605SampleQueue == nullptr || samples == nullptr)
    return 0;
  size_t count = 0;
  while (count < capacity &&
         xQueueReceive(rfp605SampleQueue, &samples[count], 0) == pdTRUE) {
    ++count;
  }
  return count;
}

uint32_t rfp605DroppedSamples() {
  return rfp605QueueDropCount.load(std::memory_order_relaxed);
}
#endif

size_t rfp605Update(Rfp605Sample *samples, size_t capacity) {
  const uint32_t now = millis();
  if (now - lastScanMs < SCAN_INTERVAL_MS)
    return 0;
  lastScanMs = now;
  size_t sampleCount = 0;

  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    if ((ACTIVE_CHANNEL_MASK & (1U << channel)) == 0)
      continue;

    ChannelState &state = channels[channel];
    const uint16_t raw = readRawAverage(channel);
    const uint32_t sequence = state.sequence++;
#if defined(RFP605_GATEWAY_INTEGRATION)
    portENTER_CRITICAL(&rfp605StateMux);
    const bool pressed = updatePressedState(state, raw);
    portEXIT_CRITICAL(&rfp605StateMux);
#else
    const bool pressed = updatePressedState(state, raw);
#endif

    const uint16_t deviation = raw >= state.baselineRaw
                                   ? raw - state.baselineRaw
                                   : state.baselineRaw - raw;
    const uint32_t sampleMs = millis();

#if RFP605_DEBUG_SCAN
    const uint16_t millivolts = readMillivoltsAverage();
    Serial.printf(
        "{\"type\":\"debug\",\"id\":\"%s\",\"channel\":%u,"
        "\"seq\":%lu,\"sample_ms\":%lu,\"baseline_raw\":%u,"
        "\"press_delta_raw\":%u,\"deviation_raw\":%u,"
        "\"pressure_raw\":%u,\"voltage_mV\":%u,\"pressed\":%s}\n",
        SENSOR_IDS[channel], static_cast<unsigned>(channel),
        static_cast<unsigned long>(sequence),
        static_cast<unsigned long>(millis()),
        static_cast<unsigned>(state.baselineRaw),
        static_cast<unsigned>(state.pressDeltaRaw),
        static_cast<unsigned>(deviation), static_cast<unsigned>(raw),
        static_cast<unsigned>(millivolts), pressed ? "true" : "false");
#else
    // No record is emitted for an unpressed channel.
    if (!pressed)
      continue;

    const uint16_t millivolts = readMillivoltsAverage();
    if (samples != nullptr && sampleCount < capacity) {
      Rfp605Sample &sample = samples[sampleCount++];
      sample.channel = channel;
      sample.sequence = sequence;
      sample.sampleMs = sampleMs;
      sample.baselineRaw = state.baselineRaw;
      sample.pressDeltaRaw = state.pressDeltaRaw;
      sample.deviationRaw = deviation;
      sample.pressureRaw = raw;
      sample.voltageMv = millivolts;
    }
#endif
  }
  return sampleCount;
}

uint16_t rfp605PressedMask() {
  uint16_t mask = 0;
#if defined(RFP605_GATEWAY_INTEGRATION)
  portENTER_CRITICAL(&rfp605StateMux);
#endif
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    if ((ACTIVE_CHANNEL_MASK & (1U << channel)) && channels[channel].pressed)
      mask |= static_cast<uint16_t>(1U << channel);
  }
#if defined(RFP605_GATEWAY_INTEGRATION)
  portEXIT_CRITICAL(&rfp605StateMux);
#endif
  return mask;
}

#if defined(RFP605_SANDBOX) && !defined(RFP605_GATEWAY_INTEGRATION)
void setup() {
  Serial.begin(115200);
  delay(300);
  rfp605Begin();
}

void loop() {
  Rfp605Sample samples[CHANNEL_COUNT] = {};
  const size_t count = rfp605Update(samples, CHANNEL_COUNT);

  static uint16_t previousMask = 0;
  const uint16_t currentMask = rfp605PressedMask();
  const uint16_t changedMask = currentMask ^ previousMask;
  for (uint8_t channel = 0; channel < CHANNEL_COUNT; ++channel) {
    const uint16_t bit = static_cast<uint16_t>(1U << channel);
    if ((changedMask & bit) == 0)
      continue;

    if ((currentMask & bit) == 0) {
      Serial.printf("[RFP605] RELEASED C%u | pressed mask=0x%04X\n",
                    static_cast<unsigned>(channel),
                    static_cast<unsigned>(currentMask));
      continue;
    }

    const Rfp605Sample *pressedSample = nullptr;
    for (size_t i = 0; i < count; ++i) {
      if (samples[i].channel == channel) {
        pressedSample = &samples[i];
        break;
      }
    }
    if (pressedSample != nullptr) {
      Serial.printf(
          "[RFP605] PRESSED C%u | raw=%u baseline=%u delta=%u "
          "voltage=%u mV | mask=0x%04X\n",
          static_cast<unsigned>(channel),
          static_cast<unsigned>(pressedSample->pressureRaw),
          static_cast<unsigned>(pressedSample->baselineRaw),
          static_cast<unsigned>(pressedSample->deviationRaw),
          static_cast<unsigned>(pressedSample->voltageMv),
          static_cast<unsigned>(currentMask));
    } else {
      Serial.printf("[RFP605] PRESSED C%u | pressed mask=0x%04X\n",
                    static_cast<unsigned>(channel),
                    static_cast<unsigned>(currentMask));
    }
  }
  previousMask = currentMask;
}
#endif // standalone sensor sandbox

#endif // RFP605_SANDBOX || RFP605_GATEWAY_INTEGRATION
