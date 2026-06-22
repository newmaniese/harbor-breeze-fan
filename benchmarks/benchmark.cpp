#include <iostream>
#include <vector>
#include <chrono>
#include <cstring>
#include "include/harbor_breeze.h"

// Mock some things if needed, but we have harbor_breeze.cpp
// We can just compile harbor_breeze.cpp and link it.

int main() {
    const char* cmd10[10] = { "SL","SL","SS","LS","LL","SS","LL","SS","LL","SR" };
    uint16_t out[HB_HUB_MAX_PULSES];
    int maxOut = HB_HUB_MAX_PULSES;

    // Warm up
    for (int i = 0; i < 1000; i++) {
        harborBreezeHubCommandPulses(cmd10, out, maxOut);
    }

    auto start = std::chrono::high_resolution_clock::now();
    int iterations = 1000000;
    int total_idx = 0;
    for (int i = 0; i < iterations; i++) {
        total_idx += harborBreezeHubCommandPulses(cmd10, out, maxOut);
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "Time: " << diff.count() << " s" << std::endl;
    std::cout << "Iterations: " << iterations << std::endl;
    std::cout << "Time per iteration: " << (diff.count() / iterations) * 1e9 << " ns" << std::endl;
    std::cout << "Total idx: " << total_idx << std::endl;

    return 0;
}
