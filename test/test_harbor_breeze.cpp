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

static void hubSymbolsToPulses(const char* const symbols[], int count, uint16_t* out, int* idx) {
    for (int i = 0; i < count; i++) {
        const char* sym = symbols[i];
        if (strcmp(sym, "SS") == 0) { out[(*idx)++] = HB_HUB_SHORT_ON; out[(*idx)++] = HB_HUB_SHORT_OFF; }
        else if (strcmp(sym, "SL") == 0) { out[(*idx)++] = HB_HUB_SHORT_ON; out[(*idx)++] = HB_HUB_LONG_OFF; }
        else if (strcmp(sym, "LL") == 0) { out[(*idx)++] = HB_HUB_LONG_ON; out[(*idx)++] = HB_HUB_LONG_OFF; }
        else if (strcmp(sym, "LS") == 0) { out[(*idx)++] = HB_HUB_LONG_ON; out[(*idx)++] = HB_HUB_SHORT_OFF; }
        else if (strcmp(sym, "SR") == 0) { out[(*idx)++] = HB_HUB_SHORT_ON; out[(*idx)++] = HB_HUB_REST; }
    }
}

void test_harborBreezeHubDecodePulses_basic() {
    uint16_t pulses[100];
    int idx = 0;
    const char* remote0[] = { "SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL" };
    const char* light_toggle[] = { "SL","SL","SS","LS","LL","SS","LL","SS","LL","SR" };

    hubSymbolsToPulses(remote0, 15, pulses, &idx);
    hubSymbolsToPulses(light_toggle, 10, pulses, &idx);

    char symbols_buf[120];
    const char* matched_cmd = nullptr;
    int res = harborBreezeHubDecodePulses(pulses, idx, symbols_buf, sizeof(symbols_buf), &matched_cmd);

    ASSERT(res == 25, "Should decode 25 symbols");
    ASSERT(matched_cmd != nullptr, "Should match a command");
    ASSERT(strcmp(matched_cmd, "light_toggle") == 0, "Should match light_toggle");
    ASSERT(strstr(symbols_buf, "SL, SL") != nullptr, "Symbols buffer should contain SL");
    ASSERT(strstr(symbols_buf, "SR") != nullptr, "Symbols buffer should contain SR");
}

void test_harborBreezeHubDecodePulses_leading_idle() {
    uint16_t pulses[100];
    pulses[0] = 6000; // Leading idle > 5000
    int idx = 1;
    const char* remote0[] = { "SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL" };
    const char* fan_off[] = { "LL","SS","LS","LS","LS","LS","LL","SS","LL","SR" };

    hubSymbolsToPulses(remote0, 15, pulses, &idx);
    hubSymbolsToPulses(fan_off, 10, pulses, &idx);

    char symbols_buf[120];
    const char* matched_cmd = nullptr;
    int res = harborBreezeHubDecodePulses(pulses, idx, symbols_buf, sizeof(symbols_buf), &matched_cmd);

    ASSERT(res == 25, "Should decode 25 symbols even with leading idle");
    ASSERT(matched_cmd != nullptr, "Should match fan_off");
    ASSERT(strcmp(matched_cmd, "fan_off") == 0, "Should match fan_off");
}

void test_harborBreezeHubDecodePulses_variations() {
    uint16_t pulses[100];
    int idx = 0;
    const char* remote0[] = { "SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL" };
    const char* fan_speed_1[] = { "LL","SL","SS","LS","LS","LS","LL","SS","LL","SR" };

    hubSymbolsToPulses(remote0, 15, pulses, &idx);
    hubSymbolsToPulses(fan_speed_1, 10, pulses, &idx);

    // Add variations within tolerance (HUB_TOL = 150)
    pulses[0] += 100; // SL ON: 400 -> 500
    pulses[1] -= 100; // SL OFF: 950 -> 850
    pulses[idx-2] += 100; // SR ON: 400 -> 500
    pulses[idx-1] += 500; // SR OFF: 10000 -> 10500

    char symbols_buf[120];
    const char* matched_cmd = nullptr;
    int res = harborBreezeHubDecodePulses(pulses, idx, symbols_buf, sizeof(symbols_buf), &matched_cmd);

    ASSERT(res == 25, "Should decode with variations within tolerance");
    ASSERT(matched_cmd != nullptr && strcmp(matched_cmd, "fan_speed_1") == 0, "Should match fan_speed_1");

    // Variations outside tolerance
    pulses[0] = 400 + 200; // 600 > 400 + 150
    res = harborBreezeHubDecodePulses(pulses, idx, symbols_buf, sizeof(symbols_buf), &matched_cmd);
    ASSERT(res == 0, "Should NOT decode with variations outside tolerance");
}

void test_harborBreezeHubDecodePulses_invalid() {
    uint16_t pulses[100];
    char symbols_buf[120];
    const char* matched_cmd = nullptr;

    ASSERT(harborBreezeHubDecodePulses(nullptr, 100, symbols_buf, 120, &matched_cmd) == 0, "NULL pulses");
    ASSERT(harborBreezeHubDecodePulses(pulses, 49, symbols_buf, 120, &matched_cmd) == 0, "Too few pulses");
    ASSERT(harborBreezeHubDecodePulses(pulses, 100, nullptr, 120, &matched_cmd) == 0, "NULL buffer");
    ASSERT(harborBreezeHubDecodePulses(pulses, 100, symbols_buf, 79, &matched_cmd) == 0, "Buffer too small");
}

void test_harborBreezeHubDecodePulses_unknown() {
    uint16_t pulses[100];
    int idx = 0;
    const char* remote0[] = { "SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL","SL" };
    const char* unknown_cmd[] = { "SS","SS","SS","SS","SS","SS","SS","SS","SS","SR" };

    hubSymbolsToPulses(remote0, 15, pulses, &idx);
    hubSymbolsToPulses(unknown_cmd, 10, pulses, &idx);

    char symbols_buf[120];
    const char* matched_cmd = nullptr;
    int res = harborBreezeHubDecodePulses(pulses, idx, symbols_buf, sizeof(symbols_buf), &matched_cmd);

    ASSERT(res == 25, "Should decode 25 symbols even if command unknown");
    ASSERT(matched_cmd == nullptr, "Should NOT match a known command");
    ASSERT(strstr(symbols_buf, "SS, SS, SS") != nullptr, "Should have SS symbols in buffer");
}

int main() {
    test_harborBreezeBuildPulses_invalid_inputs();
    test_harborBreezeBuildPulses_basic_timing();
    test_harborBreezeBuildPulses_zero_bit();
    test_harborBreezeBuildPulses_buffer_limit();
    test_harborBreezeBuildPulses_maxOut_limit_during_gap();
    test_harborBreezeBuildPulses_full_code();

    test_harborBreezeHubDecodePulses_basic();
    test_harborBreezeHubDecodePulses_leading_idle();
    test_harborBreezeHubDecodePulses_variations();
    test_harborBreezeHubDecodePulses_invalid();
    test_harborBreezeHubDecodePulses_unknown();

    printf("All tests passed!\n");
    return 0;
}
