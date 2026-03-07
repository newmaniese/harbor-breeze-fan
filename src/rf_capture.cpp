#include "rf_capture.h"

#define RF_RECV_PIN 5

static volatile uint16_t captureBuf[RF_CAPTURE_MAX_PULSES];
static volatile int captureLen = 0;
static volatile unsigned long lastEdgeMicros = 0;
static volatile bool capturing = false;

static uint16_t lastBuf[RF_CAPTURE_MAX_PULSES];
static int lastLen = 0;
static uint32_t lastSeq = 0;
static bool hasNew = false;

static void IRAM_ATTR rfCaptureIsr() {
  unsigned long now = micros();
  if (capturing && captureLen < RF_CAPTURE_MAX_PULSES) {
    unsigned long delta = now - lastEdgeMicros;
    if (delta > 0xFFFF) delta = 0xFFFF;
    captureBuf[captureLen++] = (uint16_t)delta;
  }
  lastEdgeMicros = now;
  capturing = true;
}

void rfCaptureBegin() {
  pinMode(RF_RECV_PIN, INPUT_PULLDOWN);
  captureLen = 0;
  capturing = false;
  lastEdgeMicros = micros();
  attachInterrupt(digitalPinToInterrupt(RF_RECV_PIN), rfCaptureIsr, CHANGE);
  printf("[HB] rfCaptureBegin: pin=%d\n", RF_RECV_PIN);
}

void rfCaptureLoop() {
  if (!capturing || captureLen == 0) return;
  unsigned long now = micros();
  if (now - lastEdgeMicros < (unsigned long)RF_CAPTURE_GAP_US) return;

  // #region agent log
  bool justCaptured = false;
  // #endregion

  noInterrupts();
  int len = captureLen;
  captureLen = 0;
  capturing = false;
  if (len >= RF_CAPTURE_MIN_PULSES) {
    uint16_t mn = 0xFFFF, mx = 0;
    int checkN = (16 < len) ? 16 : len;
    for (int i = 0; i < checkN; i++) {
      uint16_t v = captureBuf[i];
      if (v < mn) mn = v;
      if (v > mx) mx = v;
    }
    if (mn >= (uint16_t)RF_CAPTURE_MIN_PULSE_US) {
      for (int i = 0; i < len && i < RF_CAPTURE_MAX_PULSES; i++) {
        lastBuf[i] = captureBuf[i];
      }
      lastLen = len;
      lastSeq++;
      hasNew = true;
      justCaptured = true;  // #region agent log
    }
  }
  interrupts();
  
  // #region agent log
  if (justCaptured) {
    printf("[HB] RF captured %d pulses, seq %lu, first4: %u %u %u %u\n", 
           lastLen, (unsigned long)lastSeq,
           lastLen > 0 ? lastBuf[0] : 0, lastLen > 1 ? lastBuf[1] : 0,
           lastLen > 2 ? lastBuf[2] : 0, lastLen > 3 ? lastBuf[3] : 0);
  }
  // #endregion
}

bool rfCaptureHasNew() {
  return hasNew;
}

void rfCaptureClearNew() {
  hasNew = false;
}

int rfCaptureGetLastLength() {
  return lastLen;
}

int rfCaptureGetLastPulses(uint16_t* out, int maxLen) {
  int n = lastLen;
  if (n > maxLen) n = maxLen;
  for (int i = 0; i < n; i++) {
    out[i] = lastBuf[i];
  }
  return n;
}

uint32_t rfCaptureGetLastSeq() {
  return lastSeq;
}
