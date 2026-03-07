#ifndef HARBOR_BREEZE_H
#define HARBOR_BREEZE_H

#include <Arduino.h>

// Harbor Breeze 315 MHz protocol (FCC A25-TX012). Matched to harborBreeze315 (python/app.py).
// Timing: short_delay=380 µs, long_delay=770 µs. Bit 1 = long HIGH + short LOW; 0 = short HIGH + long LOW.
// Frame = 17-bit preamble (DIP 1) + 8-bit function. Repeat 6 times with 8 ms gap.

#define HB_SHORT_US  380
#define HB_LONG_US   770
#define HB_GAP_MS    8
#define HB_REPEATS   6

// DIP 1 preamble (17 bits)
#define HB_PREAMBLE_DIP1 "00110000111111101"

// 8-bit function codes (DIP 1). See https://fccid.io/A25-TX012/User-Manual/User-manual-1937614
#define HB_FAN_FW1   "01110010"  // Winter speed 1
#define HB_FAN_FW2   "10110010"
#define HB_FAN_FW3   "00110010"
#define HB_FAN_FW4   "11010010"
#define HB_FAN_FW5   "01010010"
#define HB_FAN_FW6   "10010010"
#define HB_FAN_FWT   "11110010"  // Fan on/off
#define HB_FAN_FWN   "00010010"  // Nature Breeze
#define HB_FAN_FS1   "01111010"  // Summer speed 1
#define HB_FAN_FS2   "10111010"
#define HB_FAN_FS3   "00111010"
#define HB_FAN_FS4   "11011010"
#define HB_FAN_FS5   "01011010"
#define HB_FAN_FS6   "10011010"
#define HB_FAN_FST   "11111010"  // Fan on/off (summer)
#define HB_FAN_FSN   "00011010"  // Nature Breeze (summer)
#define HB_FAN_FWD   "11100010"  // Winter direction
#define HB_FAN_FSD   "11101010"  // Summer direction
#define HB_LIGHT_H   "00001110"  // Home Shield
#define HB_LIGHT_T   "01101010"  // Light on/off
#define HB_LIGHT_D   "10101010"  // Light dimming
#define HB_DELAY_O   "00100010"  // Delay off
#define HB_DELAY_2   "01101110"  // Delay 2h
#define HB_DELAY_4   "10101110"  // Delay 4h
#define HB_DELAY_8   "00101110"  // Delay 8h

// Max pulse count: 1 (leading LOW) + 25 bits * 2 + 6 repeats + 5 gaps = 1 + 300 + 5 = 306
#define HB_MAX_PULSES 512

// Build pulse array for a full code string (preamble + function). Returns pulse count.
// Out buffer must hold at least HB_MAX_PULSES entries. Repeats frame HB_REPEATS times with 8 ms gap.
int harborBreezeBuildPulses(const char* code, uint16_t* out, int maxOut);

// Build pulses for a known command. code is the 8-bit function (e.g. HB_LIGHT_T). Uses DIP 1 preamble.
int harborBreezeCommandPulses(const char* func8, uint16_t* out, int maxOut);

// --- Hub protocol (from github.com/enlilodisho/harbor-breeze-hub) ---
// Timings in µs: SHORT_ON 400, SHORT_OFF 500, LONG_ON 850, LONG_OFF 950, REST 10000.
// Frame = remote_id (15 symbol pairs) + command (10 pairs); repeated 12× with no gap. First edge HIGH.
#define HB_HUB_SHORT_ON   400
#define HB_HUB_SHORT_OFF  500
#define HB_HUB_LONG_ON    850
#define HB_HUB_LONG_OFF   950
#define HB_HUB_REST       10000
#define HB_HUB_REPEATS    12
// One frame = 25 symbol pairs = 50 timings; 12 repeats = 600.
#define HB_HUB_MAX_PULSES 1024

// Build hub-format pulses for remote "0" + 10-symbol command. Out must hold HB_HUB_MAX_PULSES.
int harborBreezeHubCommandPulses(const char* cmd10[10], uint16_t* out, int maxOut);

int harborBreezeHubLightTogglePulses(uint16_t* out, int maxOut);
int harborBreezeHubLightDimPulses(uint16_t* out, int maxOut);
int harborBreezeHubFanPowerPulses(uint16_t* out, int maxOut);
int harborBreezeHubFanSpeedPulses(int speed, uint16_t* out, int maxOut);  // speed 1..6
int harborBreezeHubBreezePulses(uint16_t* out, int maxOut);
int harborBreezeHubRotateCcwPulses(uint16_t* out, int maxOut);   // summer
int harborBreezeHubRotateCwPulses(uint16_t* out, int maxOut);   // winter

#endif // HARBOR_BREEZE_H
