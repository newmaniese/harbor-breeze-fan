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

int harborBreezeHubCommandPulses(const char* const cmd10[10], uint16_t* out, int maxOut) {
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

// Single source of truth for hub commands (10 symbols each). Used for both encode and decode.
static const struct {
  const char* name;
  const char* syms[10];
} HUB_CMDS[] = {
  { "light_toggle",       { "SL","SL","SS","LS","LL","SS","LL","SS","LL","SR" } },
  { "light_dim",          { "LL","SS","LL","SS","LL","SS","LL","SS","LL","SR" } },
  { "fan_off",            { "LL","SS","LS","LS","LS","LS","LL","SS","LL","SR" } },
  { "fan_speed_1",        { "LL","SL","SS","LS","LS","LS","LL","SS","LL","SR" } },
  { "fan_speed_2",        { "LL","SS","LL","SS","LS","LS","LL","SS","LL","SR" } },
  { "fan_speed_3",        { "LL","SL","SL","SS","LS","LS","LL","SS","LL","SR" } },
  { "fan_speed_4",        { "LL","SS","LS","LL","SS","LS","LL","SS","LL","SR" } },
  { "fan_speed_5",        { "LL","SL","SS","LL","SS","LS","LL","SS","LL","SR" } },
  { "fan_speed_6",        { "LL","SS","LL","SL","SS","LS","LL","SS","LL","SR" } },
  { "nature_breeze",      { "LL","SL","SL","SL","SS","LS","LL","SS","LL","SR" } },
  { "fan_direction_summer", { "LL","SS","LS","LS","LL","SS","LL","SS","LL","SR" } },
  { "fan_direction_winter", { "LL","SS","LS","LS","LL","SL","SL","SS","LL","SR" } },
  { "home_shield",        { "SL","SL","SL","SS","LS","LS","LL","SR","SL","SL" } },
};
static const int HUB_CMD_COUNT = (int)(sizeof(HUB_CMDS) / sizeof(HUB_CMDS[0]));

int harborBreezeHubLightTogglePulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_CMDS[0].syms, out, maxOut);
}
int harborBreezeHubLightDimPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_CMDS[1].syms, out, maxOut);
}
int harborBreezeHubFanPowerPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_CMDS[2].syms, out, maxOut);
}
int harborBreezeHubFanSpeedPulses(int speed, uint16_t* out, int maxOut) {
  if (speed < 1 || speed > 6) return 0;
  return harborBreezeHubCommandPulses(HUB_CMDS[2 + speed].syms, out, maxOut);
}
int harborBreezeHubBreezePulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_CMDS[9].syms, out, maxOut);
}
int harborBreezeHubRotateCcwPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_CMDS[10].syms, out, maxOut);
}
int harborBreezeHubRotateCwPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_CMDS[11].syms, out, maxOut);
}
int harborBreezeHubHomeShieldPulses(uint16_t* out, int maxOut) {
  return harborBreezeHubCommandPulses(HUB_CMDS[12].syms, out, maxOut);
}

// --- Hub decode: classify (on_us, off_us) pairs into SS, SL, LL, LS, SR (tolerance ±150 µs, REST ±2000). ---
static const int HUB_TOL = 150;
static const int HUB_REST_MIN = 8000;
static const int HUB_REST_MAX = 12000;

static bool hubPairToSymbol(uint16_t on_us, uint16_t off_us, char* out) {
  auto near = [](uint16_t v, int target, int tol) { return (v >= (unsigned)(target - tol)) && (v <= (unsigned)(target + tol)); };
  if (near(on_us, 400, HUB_TOL) && near(off_us, 500, HUB_TOL)) { out[0] = 'S'; out[1] = 'S'; out[2] = '\0'; return true; }
  if (near(on_us, 400, HUB_TOL) && near(off_us, 950, HUB_TOL)) { out[0] = 'S'; out[1] = 'L'; out[2] = '\0'; return true; }
  if (near(on_us, 850, HUB_TOL) && near(off_us, 950, HUB_TOL)) { out[0] = 'L'; out[1] = 'L'; out[2] = '\0'; return true; }
  if (near(on_us, 850, HUB_TOL) && near(off_us, 500, HUB_TOL)) { out[0] = 'L'; out[1] = 'S'; out[2] = '\0'; return true; }
  if (near(on_us, 400, HUB_TOL) && off_us >= (uint16_t)HUB_REST_MIN && off_us <= (uint16_t)HUB_REST_MAX) { out[0] = 'S'; out[1] = 'R'; out[2] = '\0'; return true; }
  return false;
}

int harborBreezeHubDecodePulses(const uint16_t* pulses, int len, char* symbols_buf, size_t buf_sz, const char** matched_cmd) {
  if (!pulses || len < 50 || !symbols_buf || buf_sz < 80) return 0;
  if (matched_cmd) *matched_cmd = nullptr;
  char syms[25][3];
  for (int start = (pulses[0] > 5000) ? 1 : 0; start <= 2 && start + 50 <= len; start++) {
    int i;
    for (i = 0; i < 25; i++) {
      uint16_t on_u = pulses[start + i * 2];
      uint16_t off_u = pulses[start + i * 2 + 1];
      if (!hubPairToSymbol(on_u, off_u, syms[i])) break;
    }
    if (i < 25) continue;
    size_t pos = 0;
    for (int k = 0; k < 25 && pos + 4 <= buf_sz; k++) {
      if (k) { symbols_buf[pos++] = ','; symbols_buf[pos++] = ' '; }
      symbols_buf[pos++] = syms[k][0];
      symbols_buf[pos++] = syms[k][1];
    }
    symbols_buf[pos] = '\0';
    for (int c = 0; c < HUB_CMD_COUNT && matched_cmd; c++) {
      bool match = true;
      for (int j = 0; j < 10 && match; j++)
        match = (syms[15 + j][0] == HUB_CMDS[c].syms[j][0] && syms[15 + j][1] == HUB_CMDS[c].syms[j][1]);
      if (match) { *matched_cmd = HUB_CMDS[c].name; break; }
    }
    return 25;
  }
  return 0;
}
