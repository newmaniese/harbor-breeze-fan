#include "harbor_breeze.h"
#include <cstring>

// Match harborBreeze315: bit 1 = long HIGH (770) + short LOW (380); bit 0 = short HIGH (380) + long LOW (770).
static void appendBit(char bit, uint16_t* out, int* idx, int maxOut) {
  if (*idx + 2 > maxOut) return;
  if (bit == '1') {
    out[(*idx)++] = HB_LONG_US;    // HIGH (long)
    out[(*idx)++] = HB_SHORT_US;   // LOW (short)
  } else if (bit == '0') {
    out[(*idx)++] = HB_SHORT_US;   // HIGH (short)
    out[(*idx)++] = HB_LONG_US;    // LOW (long)
  }
}

static void appendGap(uint16_t* out, int* idx, int maxOut) {
  if (*idx >= maxOut) return;
  // 8 ms LOW (single duration in alternating HIGH/LOW; we're after a LOW so this extends LOW)
  out[(*idx)++] = (uint16_t)(HB_GAP_MS * 1000);
}

int harborBreezeBuildPulses(const char* code, uint16_t* out, int maxOut) {
  if (!code || !out || maxOut < 2) return 0;
  int idx = 0;
  // Match Python: pin starts LOW; first bit is '0' = 770µs LOW then 380µs HIGH. Prepend initial 770µs LOW.
  if (idx < maxOut) out[idx++] = HB_LONG_US;
  for (int r = 0; r < HB_REPEATS && idx < maxOut; r++) {
    for (const char* p = code; *p && idx < maxOut; p++)
      appendBit(*p, out, &idx, maxOut);
    if (r < HB_REPEATS - 1)
      appendGap(out, &idx, maxOut);
  }
  return idx;
}

int harborBreezeCommandPulses(const char* func8, uint16_t* out, int maxOut) {
  if (!func8 || !out || maxOut < HB_MAX_PULSES) return 0;
  // Build full code: preamble (17) + function (8) = 25 bits
  char code[26];
  size_t plen = strlen(HB_PREAMBLE_DIP1);
  size_t flen = strlen(func8);
  if (plen != 17 || flen != 8) return 0;
  memcpy(code, HB_PREAMBLE_DIP1, 18);
  memcpy(code + 17, func8, 9);
  return harborBreezeBuildPulses(code, out, maxOut);
}

// --- Hub protocol: symbol pairs (SS, SL, LL, LS, SR) → ON,OFF µs. First edge HIGH. ---
static void hubAppendSymbol(const char* sym, uint16_t* out, int* idx, int maxOut) {
  if (*idx + 2 > maxOut) return;
  if (sym[0] == 'S' && sym[1] == 'S') { out[(*idx)++] = HB_HUB_SHORT_ON;  out[(*idx)++] = HB_HUB_SHORT_OFF; return; }
  if (sym[0] == 'S' && sym[1] == 'L') { out[(*idx)++] = HB_HUB_SHORT_ON;  out[(*idx)++] = HB_HUB_LONG_OFF;  return; }
  if (sym[0] == 'L' && sym[1] == 'L') { out[(*idx)++] = HB_HUB_LONG_ON;   out[(*idx)++] = HB_HUB_LONG_OFF;  return; }
  if (sym[0] == 'L' && sym[1] == 'S') { out[(*idx)++] = HB_HUB_LONG_ON;   out[(*idx)++] = HB_HUB_SHORT_OFF; return; }
  if (sym[0] == 'S' && sym[1] == 'R') { out[(*idx)++] = HB_HUB_SHORT_ON;  out[(*idx)++] = (uint16_t)HB_HUB_REST; return; }
}

// Remote "0" = 15× SL. One frame = 15 + 10 = 25 symbol pairs = 50 timings.
static const char* HUB_REMOTE0[] = { "SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL" };

int harborBreezeHubCommandPulses(const char* cmd10[10], uint16_t* out, int maxOut) {
  if (!out || !cmd10 || maxOut < 50 * HB_HUB_REPEATS) return 0;
  int idx = 0;
  for (int r = 0; r < HB_HUB_REPEATS && idx + 50 <= maxOut; r++) {
    for (int i = 0; i < 15 && idx + 2 <= maxOut; i++)
      hubAppendSymbol(HUB_REMOTE0[i], out, &idx, maxOut);
    for (int i = 0; i < 10 && idx + 2 <= maxOut; i++)
      hubAppendSymbol(cmd10[i], out, &idx, maxOut);
  }
  return idx;
}

static const char* HUB_LIGHT_POWER[] = { "SL","SL","SS","LS","LL","SS","LL","SS","LL","SR" };
static const char* HUB_LIGHT_DIM[]   = { "LL","SS","LL","SS","LL","SS","LL","SS","LL","SR" };
static const char* HUB_FAN_POWER[]  = { "LL","SS","LS","LS","LS","LS","LL","SS","LL","SR" };
static const char* HUB_FAN_SPEED_1[] = { "LL","SL","SS","LS","LS","LS","LL","SS","LL","SR" };
static const char* HUB_FAN_SPEED_2[] = { "LL","SS","LL","SS","LS","LS","LL","SS","LL","SR" };
static const char* HUB_FAN_SPEED_3[] = { "LL","SL","SL","SS","LS","LS","LL","SS","LL","SR" };
static const char* HUB_FAN_SPEED_4[] = { "LL","SS","LS","LL","SS","LS","LL","SS","LL","SR" };
static const char* HUB_FAN_SPEED_5[] = { "LL","SL","SS","LL","SS","LS","LL","SS","LL","SR" };
static const char* HUB_FAN_SPEED_6[] = { "LL","SS","LL","SL","SS","LS","LL","SS","LL","SR" };
static const char* HUB_FAN_BREEZE[]  = { "LL","SL","SL","SL","SS","LS","LL","SS","LL","SR" };
static const char* HUB_ROTATE_CCW[] = { "LL","SS","LS","LS","LL","SS","LL","SS","LL","SR" };
static const char* HUB_ROTATE_CW[]  = { "LL","SS","LS","LS","LL","SL","SL","SS","LL","SR" };

int harborBreezeHubLightTogglePulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_LIGHT_POWER, out, maxOut);
}
int harborBreezeHubLightDimPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_LIGHT_DIM, out, maxOut);
}
int harborBreezeHubFanPowerPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_FAN_POWER, out, maxOut);
}
int harborBreezeHubFanSpeedPulses(int speed, uint16_t* out, int maxOut) {
  const char** cmd = nullptr;
  if (speed == 1) cmd = HUB_FAN_SPEED_1;
  else if (speed == 2) cmd = HUB_FAN_SPEED_2;
  else if (speed == 3) cmd = HUB_FAN_SPEED_3;
  else if (speed == 4) cmd = HUB_FAN_SPEED_4;
  else if (speed == 5) cmd = HUB_FAN_SPEED_5;
  else if (speed == 6) cmd = HUB_FAN_SPEED_6;
  return cmd ? harborBreezeHubCommandPulses(cmd, out, maxOut) : 0;
}
int harborBreezeHubBreezePulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_FAN_BREEZE, out, maxOut);
}
int harborBreezeHubRotateCcwPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_ROTATE_CCW, out, maxOut);
}
int harborBreezeHubRotateCwPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_ROTATE_CW, out, maxOut);
}
