#include "../include/harbor_breeze.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
            exit(1); \
        } \
    } while (0)

void test_harborBreezeBuildPulses_invalid_inputs() {
    uint16_t out[10];

    ASSERT(harborBreezeBuildPulses(NULL, out, 10) == 0, "Should return 0 for NULL code");
    ASSERT(harborBreezeBuildPulses("101", NULL, 10) == 0, "Should return 0 for NULL out");
    ASSERT(harborBreezeBuildPulses("101", out, 1) == 0, "Should return 0 for maxOut < 2");
}

void test_harborBreezeBuildPulses_basic_timing() {
    uint16_t out[100];
    memset(out, 0, sizeof(out));

    // code with just one bit: '1'
    // '1' is long HIGH (770), short LOW (380)
    int count = harborBreezeBuildPulses("1", out, 100);

    // First element is ALWAYS long low preamble (770).
    // Then the bit: 770, 380
    // But since HB_REPEATS is 6, we expect:
    // Preamble LOW (1)
    // Repeat 0: '1' (2)
    // Repeat 0 Gap (1)
    // Repeat 1: '1' (2)
    // Repeat 1 Gap (1)
    // ...
    // Total for code="1": 1 + 6 * (2) + 5 = 1 + 12 + 5 = 18 pulses.

    ASSERT(count == 18, "Count for '1' with 6 repeats should be 18");
    ASSERT(out[0] == HB_LONG_US, "Initial pulse should be long LOW");

    // Repeat 0
    ASSERT(out[1] == HB_LONG_US, "Bit 1: long HIGH");
    ASSERT(out[2] == HB_SHORT_US, "Bit 1: short LOW");
    ASSERT(out[3] == HB_GAP_MS * 1000, "Gap");

    // Repeat 1
    ASSERT(out[4] == HB_LONG_US, "Bit 1: long HIGH");
    ASSERT(out[5] == HB_SHORT_US, "Bit 1: short LOW");
    ASSERT(out[6] == HB_GAP_MS * 1000, "Gap");
}

void test_harborBreezeBuildPulses_zero_bit() {
    uint16_t out[100];
    memset(out, 0, sizeof(out));

    int count = harborBreezeBuildPulses("0", out, 100);
    ASSERT(count == 18, "Count for '0' with 6 repeats should be 18");

    ASSERT(out[0] == HB_LONG_US, "Initial pulse should be long LOW");
    // Repeat 0
    ASSERT(out[1] == HB_SHORT_US, "Bit 0: short HIGH");
    ASSERT(out[2] == HB_LONG_US, "Bit 0: long LOW");
    ASSERT(out[3] == HB_GAP_MS * 1000, "Gap");
}

void test_harborBreezeBuildPulses_buffer_limit() {
    uint16_t out[10];
    memset(out, 0, sizeof(out));

    // Only allow 4 pulses
    int count = harborBreezeBuildPulses("10", out, 4);
    ASSERT(count == 4, "Should truncate bit 0 but add gap as out[3]");

    ASSERT(out[0] == HB_LONG_US, "Initial long LOW");
    ASSERT(out[1] == HB_LONG_US, "Bit '1' HIGH");
    ASSERT(out[2] == HB_SHORT_US, "Bit '1' LOW");
    // Bit '0' needs 2 spots, but only 1 spot left, so appendBit returns without appending.
    // Then appendGap sees idx=3 < maxOut=4, so it appends the gap.
    ASSERT(out[3] == HB_GAP_MS * 1000, "Gap is appended after bit '0' is truncated");
}

void test_harborBreezeBuildPulses_maxOut_limit_during_gap() {
    uint16_t out[10];
    memset(out, 0, sizeof(out));

    // Test code "1".
    // Need: initial (1) + bit '1' (2) + gap (1) = 4.
    // Set maxOut to 3, so gap is truncated.
    int count = harborBreezeBuildPulses("1", out, 3);
    ASSERT(count == 3, "Should truncate before gap if maxOut is reached");
    ASSERT(out[0] == HB_LONG_US, "Initial");
    ASSERT(out[1] == HB_LONG_US, "Bit '1' HIGH");
    ASSERT(out[2] == HB_SHORT_US, "Bit '1' LOW");
}

void test_harborBreezeBuildPulses_full_code() {
    uint16_t out[1000];
    memset(out, 0, sizeof(out));

    // Just passing a short string that represents a real partial sequence
    const char* code = "00110";
    int count = harborBreezeBuildPulses(code, out, 1000);

    // initial + 6 * (5 * 2) + 5 = 1 + 60 + 5 = 66
    ASSERT(count == 66, "Full code length calculation");

    ASSERT(out[0] == HB_LONG_US, "init");
    // '0'
    ASSERT(out[1] == HB_SHORT_US, "0 HIGH");
    ASSERT(out[2] == HB_LONG_US, "0 LOW");
    // '0'
    ASSERT(out[3] == HB_SHORT_US, "0 HIGH");
    ASSERT(out[4] == HB_LONG_US, "0 LOW");
    // '1'
    ASSERT(out[5] == HB_LONG_US, "1 HIGH");
    ASSERT(out[6] == HB_SHORT_US, "1 LOW");
    // '1'
    ASSERT(out[7] == HB_LONG_US, "1 HIGH");
    ASSERT(out[8] == HB_SHORT_US, "1 LOW");
    // '0'
    ASSERT(out[9] == HB_SHORT_US, "0 HIGH");
    ASSERT(out[10] == HB_LONG_US, "0 LOW");
    // gap
    ASSERT(out[11] == HB_GAP_MS * 1000, "Gap");
}

void test_harborBreezeCommandPulses_invalid_inputs() {
    uint16_t out[HB_MAX_PULSES];
    ASSERT(harborBreezeCommandPulses(NULL, out, HB_MAX_PULSES) == 0, "Should return 0 for NULL func8");
    ASSERT(harborBreezeCommandPulses("01101010", NULL, HB_MAX_PULSES) == 0, "Should return 0 for NULL out");
    ASSERT(harborBreezeCommandPulses("01101010", out, HB_MAX_PULSES - 1) == 0, "Should return 0 for maxOut < HB_MAX_PULSES");
}

void test_harborBreezeCommandPulses_invalid_length() {
    uint16_t out[HB_MAX_PULSES];
    ASSERT(harborBreezeCommandPulses("0110101", out, HB_MAX_PULSES) == 0, "Should return 0 for 7-char func8");
    ASSERT(harborBreezeCommandPulses("011010100", out, HB_MAX_PULSES) == 0, "Should return 0 for 9-char func8");
}

void test_harborBreezeCommandPulses_valid_command() {
    uint16_t out[HB_MAX_PULSES];
    memset(out, 0, sizeof(out));

    // HB_LIGHT_T = "01101010"
    // HB_PREAMBLE_DIP1 = "00110000111111101"
    // Combined = "00110000111111101" + "01101010" = "0011000011111110101101010" (25 bits)

    int count = harborBreezeCommandPulses("01101010", out, HB_MAX_PULSES);

    // initial (1) + 6 * (25 * 2) + 5 = 1 + 300 + 5 = 306
    ASSERT(count == 306, "Valid command should produce 306 pulses");

    ASSERT(out[0] == HB_LONG_US, "init");
    // First bit of preamble is '0'
    ASSERT(out[1] == HB_SHORT_US, "Preamble bit 0 HIGH");
    ASSERT(out[2] == HB_LONG_US, "Preamble bit 0 LOW");

    // Preamble is 17 bits.
    // Bit 17 (first bit of func8) is index 17 in 0-indexed string.
    // Pulse index = 1 + 17*2 = 35.
    // func8[0] = '0'
    ASSERT(out[35] == HB_SHORT_US, "Func bit 0 HIGH");
    ASSERT(out[36] == HB_LONG_US, "Func bit 0 LOW");

    // func8[1] = '1'
    ASSERT(out[37] == HB_LONG_US, "Func bit 1 HIGH");
    ASSERT(out[38] == HB_SHORT_US, "Func bit 1 LOW");
}

int main() {
    test_harborBreezeBuildPulses_invalid_inputs();
    test_harborBreezeBuildPulses_basic_timing();
    test_harborBreezeBuildPulses_zero_bit();
    test_harborBreezeBuildPulses_buffer_limit();
    test_harborBreezeBuildPulses_maxOut_limit_during_gap();
    test_harborBreezeBuildPulses_full_code();

    test_harborBreezeCommandPulses_invalid_inputs();
    test_harborBreezeCommandPulses_invalid_length();
    test_harborBreezeCommandPulses_valid_command();

    printf("All tests passed!\n");
    return 0;
}
