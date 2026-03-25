#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cstring>
#include "harbor_breeze.h"

void test_harborBreezeCommandPulses_HappyPath() {
    uint16_t out[HB_MAX_PULSES];
    const char* func = HB_LIGHT_T; // "01101010"
    int count = harborBreezeCommandPulses(func, out, HB_MAX_PULSES);

    // Expected count: 1 (leading) + (17 preamble + 8 function) * 2 bits * 6 repeats + 5 gaps
    // Wait, let's look at the code:
    // harborBreezeBuildPulses(code, out, maxOut):
    // if (idx < maxOut) out[idx++] = HB_LONG_US; // 1
    // for (int r = 0; r < HB_REPEATS && idx < maxOut; r++) { // 6 repeats
    //   for (const char* p = code; *p && idx < maxOut; p++) // 25 bits
    //     appendBit(*p, out, &idx, maxOut); // each bit is 2 pulses
    //   if (r < HB_REPEATS - 1)
    //     appendGap(out, &idx, maxOut); // 1 pulse
    // }
    // Total = 1 + 6 * (25 * 2) + 5 = 1 + 300 + 5 = 306.

    assert(count == 306);
    assert(out[0] == HB_LONG_US);

    // Verify first bit of preamble '0' in first repeat
    // out[1] = HB_SHORT_US, out[2] = HB_LONG_US
    assert(out[1] == HB_SHORT_US);
    assert(out[2] == HB_LONG_US);

    // Verify last bit of function '0' in last repeat
    // Last repeat starts at idx = 1 + 5 * (50 + 1) = 1 + 255 = 256
    // Function starts at bit 17 of 25.
    // Bit 24 (last bit) is at idx 256 + 24 * 2 = 256 + 48 = 304
    // out[304] = HB_SHORT_US, out[305] = HB_LONG_US
    assert(out[304] == HB_SHORT_US);
    assert(out[305] == HB_LONG_US);

    std::cout << "test_harborBreezeCommandPulses_HappyPath passed!" << std::endl;
}

void test_harborBreezeCommandPulses_NullInputs() {
    uint16_t out[HB_MAX_PULSES];
    assert(harborBreezeCommandPulses(nullptr, out, HB_MAX_PULSES) == 0);
    assert(harborBreezeCommandPulses(HB_LIGHT_T, nullptr, HB_MAX_PULSES) == 0);
    std::cout << "test_harborBreezeCommandPulses_NullInputs passed!" << std::endl;
}

void test_harborBreezeCommandPulses_SmallBuffer() {
    uint16_t out[HB_MAX_PULSES];
    assert(harborBreezeCommandPulses(HB_LIGHT_T, out, HB_MAX_PULSES - 1) == 0);
    std::cout << "test_harborBreezeCommandPulses_SmallBuffer passed!" << std::endl;
}

void test_harborBreezeCommandPulses_InvalidLength() {
    uint16_t out[HB_MAX_PULSES];
    assert(harborBreezeCommandPulses("0110101", out, HB_MAX_PULSES) == 0);   // 7 bits
    assert(harborBreezeCommandPulses("011010101", out, HB_MAX_PULSES) == 0); // 9 bits
    std::cout << "test_harborBreezeCommandPulses_InvalidLength passed!" << std::endl;
}

int main() {
    test_harborBreezeCommandPulses_HappyPath();
    test_harborBreezeCommandPulses_NullInputs();
    test_harborBreezeCommandPulses_SmallBuffer();
    test_harborBreezeCommandPulses_InvalidLength();
    std::cout << "All tests passed!" << std::endl;
    return 0;
}
