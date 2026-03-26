#include <iostream>
#include <chrono>
#include <cstring>
#include <vector>

#define RF_CAPTURE_MAX_PULSES 512

void __attribute__((noinline)) copy_loop(volatile uint16_t* src, uint16_t* dst, int len) {
    for (int i = 0; i < len && i < RF_CAPTURE_MAX_PULSES; i++) {
        dst[i] = src[i];
    }
}

void __attribute__((noinline)) copy_memcpy(volatile uint16_t* src, uint16_t* dst, int len) {
    int copy_len = len;
    if (copy_len > RF_CAPTURE_MAX_PULSES) {
        copy_len = RF_CAPTURE_MAX_PULSES;
    }
    std::memcpy(dst, (const void*)src, copy_len * sizeof(uint16_t));
}

int main() {
    volatile uint16_t captureBuf[RF_CAPTURE_MAX_PULSES];
    uint16_t lastBuf[RF_CAPTURE_MAX_PULSES];

    for (int i=0; i<RF_CAPTURE_MAX_PULSES; ++i) {
        captureBuf[i] = i;
    }

    int len = 500;
    const int numIterations = 1000000;

    auto start_loop = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numIterations; ++i) {
        copy_loop(captureBuf, lastBuf, len);
    }
    auto end_loop = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff_loop = end_loop - start_loop;

    auto start_memcpy = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < numIterations; ++i) {
        copy_memcpy(captureBuf, lastBuf, len);
    }
    auto end_memcpy = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double, std::milli> diff_memcpy = end_memcpy - start_memcpy;

    std::cout << "Loop time: " << diff_loop.count() << " ms" << std::endl;
    std::cout << "Memcpy time: " << diff_memcpy.count() << " ms" << std::endl;
    std::cout << "Speedup: " << diff_loop.count() / diff_memcpy.count() << "x" << std::endl;

    return 0;
}
