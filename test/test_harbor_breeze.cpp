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

void test_harborBreezeHubCommandPulses_invalid_inputs() {
    uint16_t out[1024];
    const char* cmd[10] = {"SS","SS","SS","SS","SS","SS","SS","SS","SS","SR"};

    ASSERT(harborBreezeHubCommandPulses(NULL, out, 1024) == 0, "Should return 0 for NULL cmd10");
    ASSERT(harborBreezeHubCommandPulses(cmd, NULL, 1024) == 0, "Should return 0 for NULL out");
    // HB_HUB_REPEATS is 12. 50 * 12 = 600.
    ASSERT(harborBreezeHubCommandPulses(cmd, out, 599) == 0, "Should return 0 for maxOut < 600");
}

void test_harborBreezeHubCommandPulses_basic() {
    uint16_t out[1024];
    memset(out, 0, sizeof(out));
    // SS=400,500; SL=400,950; LL=850,950; LS=850,500; SR=400,10000
    const char* cmd[10] = {"SS", "SL", "LL", "LS", "SR", "SS", "SL", "LL", "LS", "SR"};

    int count = harborBreezeHubCommandPulses(cmd, out, 1024);
    ASSERT(count == 600, "Total pulses should be 600 (50 * 12)");

    // First 15 symbols (30 pulses) are HUB_REMOTE0 which is "SL" (400, 950)
    for (int i = 0; i < 15; i++) {
        ASSERT(out[i*2] == HB_HUB_SHORT_ON, "Remote0 pulse HIGH");
        ASSERT(out[i*2 + 1] == HB_HUB_LONG_OFF, "Remote0 pulse LOW");
    }

    // Next 10 symbols (20 pulses) are from our cmd array
    // 0: SS (400, 500)
    ASSERT(out[30] == 400 && out[31] == 500, "Cmd[0] SS");
    // 1: SL (400, 950)
    ASSERT(out[32] == 400 && out[33] == 950, "Cmd[1] SL");
    // 2: LL (850, 950)
    ASSERT(out[34] == 850 && out[35] == 950, "Cmd[2] LL");
    // 3: LS (850, 500)
    ASSERT(out[36] == 850 && out[37] == 500, "Cmd[3] LS");
    // 4: SR (400, 10000)
    ASSERT(out[38] == 400 && out[39] == 10000, "Cmd[4] SR");

    // Second repeat starts at index 50
    ASSERT(out[50] == HB_HUB_SHORT_ON, "Repeat 2 start HIGH");
    ASSERT(out[51] == HB_HUB_LONG_OFF, "Repeat 2 start LOW (SL)");
}


int main() {
    test_harborBreezeBuildPulses_invalid_inputs();
    test_harborBreezeBuildPulses_basic_timing();
    test_harborBreezeBuildPulses_zero_bit();
    test_harborBreezeBuildPulses_buffer_limit();
    test_harborBreezeBuildPulses_maxOut_limit_during_gap();
    test_harborBreezeBuildPulses_full_code();

    test_harborBreezeHubCommandPulses_invalid_inputs();
    test_harborBreezeHubCommandPulses_basic();

    printf("All tests passed!\n");
    return 0;
}
