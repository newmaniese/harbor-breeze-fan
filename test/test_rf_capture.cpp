#include <iostream>
#include <chrono>
#include "Arduino.h"
#include "rf_capture.h"
#include <cstring>

// Since static volatile variables are not accessible, we'll write a small C++ test
// that uses the standard rfCaptureIsr if we can call it. But rfCaptureIsr is static.
// Let's modify rf_capture.cpp slightly for the test if needed, or better, we can
// just test the performance of the memcpy vs loop directly on arrays.
// The task asks to optimize the loop in src/rf_capture.cpp:57.

int main() {
    std::cout << "We'll test it directly by compiling." << std::endl;
    return 0;
}
