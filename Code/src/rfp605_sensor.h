#pragma once

#include <Arduino.h>

struct Rfp605Sample {
  uint8_t channel;
  uint32_t sequence;
  uint32_t sampleMs;
  uint16_t baselineRaw;
  uint16_t pressDeltaRaw;
  uint16_t deviationRaw;
  uint16_t pressureRaw;
  uint16_t voltageMv;
};

void rfp605Begin();
void rfp605Start();
size_t rfp605Update(Rfp605Sample *samples, size_t capacity);
size_t rfp605ReadQueuedSamples(Rfp605Sample *samples, size_t capacity);
uint16_t rfp605PressedMask();
uint32_t rfp605DroppedSamples();
