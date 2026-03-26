#include <iostream>
#include <cassert>
#include <cstring>
#include <vector>
#include <cstdlib>

#include "harbor_breeze.h"

// Helper macros for testing
#define ASSERT_EQ(actual, expected) \
    if ((actual) != (expected)) { \
        std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #actual << " (" << (actual) << ") == " \
                  << #expected << " (" << (expected) << ")" << std::endl; \
        std::exit(1); \
    }

#define ASSERT_TRUE(condition) \
    if (!(condition)) { \
        std::cerr << "Assertion failed at " << __FILE__ << ":" << __LINE__ \
                  << ": " << #condition << " is false" << std::endl; \
        std::exit(1); \
    }

// --- Standard Protocol Tests ---

void test_build_pulses_basic() {
    uint16_t pulses[HB_MAX_PULSES];
    // A tiny dummy code to test build logic
    // Just 2 bits + 1 preamble bit = 3 bits total per repeat
    int count = harborBreezeBuildPulses("01", pulses, HB_MAX_PULSES);

    // Calculate expected length:
    // Leading LOW (1)
    // For each repeat (HB_REPEATS=6):
    //   "01":
    //     0: short HIGH + long LOW (2)
    //     1: long HIGH + short LOW (2)
    //   gap (1, except last repeat)
    // So 1 + 6 * 4 + 5 = 30
    ASSERT_EQ(count, 30);

    // First pulse is leading LOW
    ASSERT_EQ(pulses[0], HB_LONG_US);

    // First bit '0' -> short HIGH + long LOW
    ASSERT_EQ(pulses[1], HB_SHORT_US);
    ASSERT_EQ(pulses[2], HB_LONG_US);

    // Second bit '1' -> long HIGH + short LOW
    ASSERT_EQ(pulses[3], HB_LONG_US);
    ASSERT_EQ(pulses[4], HB_SHORT_US);

    // Gap
    ASSERT_EQ(pulses[5], HB_GAP_MS * 1000);
}

void test_build_pulses_edge_cases() {
    uint16_t pulses[HB_MAX_PULSES];

    // Null inputs
    ASSERT_EQ(harborBreezeBuildPulses(nullptr, pulses, HB_MAX_PULSES), 0);
    ASSERT_EQ(harborBreezeBuildPulses("01", nullptr, HB_MAX_PULSES), 0);

    // Small buffer size
    ASSERT_EQ(harborBreezeBuildPulses("01", pulses, 1), 0);

    // Buffer limits
    int count = harborBreezeBuildPulses("010101010101010101010101", pulses, 10);
    ASSERT_EQ(count, 10); // Should truncate gracefully at maxOut
}

void test_command_pulses() {
    uint16_t pulses[HB_MAX_PULSES];

    // Valid command
    int count = harborBreezeCommandPulses(HB_LIGHT_T, pulses, HB_MAX_PULSES);
    // 1 (leading) + 6 repeats * (25 bits * 2 pulses/bit) + 5 gaps = 1 + 6 * 50 + 5 = 306
    ASSERT_EQ(count, 306);

    // Invalid 8-bit functions
    ASSERT_EQ(harborBreezeCommandPulses("101", pulses, HB_MAX_PULSES), 0);
    ASSERT_EQ(harborBreezeCommandPulses(nullptr, pulses, HB_MAX_PULSES), 0);
}

// --- Hub Protocol Tests ---

void test_hub_command_pulses() {
    uint16_t pulses[HB_HUB_MAX_PULSES];

    // Light toggle command pulses
    int count = harborBreezeHubLightTogglePulses(pulses, HB_HUB_MAX_PULSES);

    // 12 repeats * 25 symbols * 2 pulses/symbol = 600
    ASSERT_EQ(count, 600);

    // First symbol is "SL" from remote 0 (15x SL)
    // SL = 400 on, 950 off
    ASSERT_EQ(pulses[0], 400); // SHORT_ON
    ASSERT_EQ(pulses[1], 950); // LONG_OFF
}

void test_hub_fan_speed_pulses() {
    uint16_t pulses[HB_HUB_MAX_PULSES];

    ASSERT_EQ(harborBreezeHubFanSpeedPulses(0, pulses, HB_HUB_MAX_PULSES), 0); // Invalid speed
    ASSERT_EQ(harborBreezeHubFanSpeedPulses(7, pulses, HB_HUB_MAX_PULSES), 0); // Invalid speed

    int count = harborBreezeHubFanSpeedPulses(3, pulses, HB_HUB_MAX_PULSES);
    ASSERT_EQ(count, 600); // Valid speed
}

// --- Hub Decode Tests ---

void test_hub_decode_pulses_basic() {
    uint16_t out[HB_HUB_MAX_PULSES];
    int len = harborBreezeHubLightTogglePulses(out, HB_HUB_MAX_PULSES);

    char symbols[100];
    const char* matched_cmd = nullptr;

    // Decode exact pulses
    int decoded = harborBreezeHubDecodePulses(out, len, symbols, sizeof(symbols), &matched_cmd);

    ASSERT_EQ(decoded, 25);
    ASSERT_TRUE(matched_cmd != nullptr);
    ASSERT_TRUE(strcmp(matched_cmd, "light_toggle") == 0);

    // Check symbols string prefix (15x SL from remote0)
    ASSERT_TRUE(strncmp(symbols, "SL, SL, SL", 10) == 0);
}

void test_hub_decode_pulses_jitter() {
    uint16_t out[HB_HUB_MAX_PULSES];
    int len = harborBreezeHubFanPowerPulses(out, HB_HUB_MAX_PULSES);

    // Add realistic jitter (+/- 100 us)
    for (int i = 0; i < len; i++) {
        // Only jitter standard pulses, not REST
        if (out[i] < 5000) {
            out[i] += (i % 2 == 0) ? 80 : -50;
        }
    }

    char symbols[100];
    const char* matched_cmd = nullptr;

    int decoded = harborBreezeHubDecodePulses(out, len, symbols, sizeof(symbols), &matched_cmd);

    ASSERT_EQ(decoded, 25);
    ASSERT_TRUE(matched_cmd != nullptr);
    ASSERT_TRUE(strcmp(matched_cmd, "fan_off") == 0);
}

void test_hub_decode_pulses_offset() {
    uint16_t out[HB_HUB_MAX_PULSES];
    int len = harborBreezeHubBreezePulses(out, HB_HUB_MAX_PULSES);

    // Simulate missed first pulse by shifting array
    uint16_t shifted[HB_HUB_MAX_PULSES];
    shifted[0] = 6000; // Large garbage pulse
    for (int i = 0; i < len - 1; i++) {
        shifted[i+1] = out[i];
    }

    char symbols[100];
    const char* matched_cmd = nullptr;

    int decoded = harborBreezeHubDecodePulses(shifted, len, symbols, sizeof(symbols), &matched_cmd);

    ASSERT_EQ(decoded, 25);
    ASSERT_TRUE(matched_cmd != nullptr);
    ASSERT_TRUE(strcmp(matched_cmd, "nature_breeze") == 0);
}

void test_hub_decode_pulses_invalid() {
    uint16_t out[HB_HUB_MAX_PULSES];
    for (int i = 0; i < 50; i++) {
        out[i] = 100; // All garbage
    }

    char symbols[100];
    const char* matched_cmd = nullptr;

    int decoded = harborBreezeHubDecodePulses(out, 50, symbols, sizeof(symbols), &matched_cmd);

    ASSERT_EQ(decoded, 0);
    ASSERT_TRUE(matched_cmd == nullptr);
}

int main() {
    std::cout << "Running Harbor Breeze tests..." << std::endl;

    test_build_pulses_basic();
    test_build_pulses_edge_cases();
    test_command_pulses();

    test_hub_command_pulses();
    test_hub_fan_speed_pulses();

    test_hub_decode_pulses_basic();
    test_hub_decode_pulses_jitter();
    test_hub_decode_pulses_offset();
    test_hub_decode_pulses_invalid();

    std::cout << "All tests passed successfully!" << std::endl;
    return 0;
}
