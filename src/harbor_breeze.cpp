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

// Remote "0" = 15× SL. Light power = SL,SL,SS,LS,LL,SS,LL,SS,LL,SR. One frame = 25 pairs = 50 timings.
static int hubBuildOneFrame(uint16_t* out, int* idx, int maxOut) {
  const char* remote0[] = { "SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL" };
  const char* lightPower[] = { "SL","SL","SS","LS","LL","SS","LL","SS","LL","SR" };
  for (int i = 0; i < 15 && *idx + 2 <= maxOut; i++)
    hubAppendSymbol(remote0[i], out, idx, maxOut);
  for (int i = 0; i < 10 && *idx + 2 <= maxOut; i++)
    hubAppendSymbol(lightPower[i], out, idx, maxOut);
  return *idx;
}

int harborBreezeHubLightTogglePulses(uint16_t* out, int maxOut) {
  if (!out || maxOut < 50 * HB_HUB_REPEATS) return 0;
  int idx = 0;
  for (int r = 0; r < HB_HUB_REPEATS && idx + 50 <= maxOut; r++)
    hubBuildOneFrame(out, &idx, maxOut);
  return idx;
}

int harborBreezeHubFanPowerPulses(uint16_t* out, int maxOut) {
  if (!out || maxOut < 50 * HB_HUB_REPEATS) return 0;
  const char* remote0[] = { "SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL" };
  const char* fanPower[] = { "LL","SS","LS","LS","LS","LS","LL","SS","LL","SR" };
  int idx = 0;
  for (int r = 0; r < HB_HUB_REPEATS && idx + 50 <= maxOut; r++) {
    for (int i = 0; i < 15 && idx + 2 <= maxOut; i++)
      hubAppendSymbol(remote0[i], out, &idx, maxOut);
    for (int i = 0; i < 10 && idx + 2 <= maxOut; i++)
      hubAppendSymbol(fanPower[i], out, &idx, maxOut);
  }
  return idx;
}
