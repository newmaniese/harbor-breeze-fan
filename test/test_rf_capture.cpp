#include "../include/rf_capture.h"
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

// Helper to simulate an edge in the RF signal
void simulatePulse(unsigned long duration_us) {
    mock_micros += duration_us;
    if (mock_isr) mock_isr();
}

void setup_test() {
    mock_micros = 0;
    rfCaptureBegin();
    rfCaptureClearNew();
    // Start capture by first edge
    simulatePulse(100);
}

void test_rfCaptureLoop_happy_path() {
    setup_test();

    // Generate RF_CAPTURE_MIN_PULSES pulses
    for (int i = 0; i < RF_CAPTURE_MIN_PULSES; i++) {
        simulatePulse(1000); // Pulse duration 1000us
    }

    ASSERT(rfCaptureHasNew() == false, "Should not be ready before gap");

    // Call loop before gap expires
    mock_micros += RF_CAPTURE_GAP_US - 1000;
    rfCaptureLoop();
    ASSERT(rfCaptureHasNew() == false, "Should not be ready until gap expires");

    // Wait for gap
    mock_micros += 2000;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == true, "Should have new capture after gap");
    ASSERT(rfCaptureGetLastLength() == RF_CAPTURE_MIN_PULSES, "Length should match");

    uint16_t out[100];
    int len = rfCaptureGetLastPulses(out, 100);
    ASSERT(len == RF_CAPTURE_MIN_PULSES, "Copied length should match");
    ASSERT(out[0] == 1000, "Pulse duration should match");
}

void test_rfCaptureLoop_too_few_pulses() {
    setup_test();

    // Generate less than RF_CAPTURE_MIN_PULSES
    for (int i = 0; i < RF_CAPTURE_MIN_PULSES - 1; i++) {
        simulatePulse(1000);
    }

    mock_micros += RF_CAPTURE_GAP_US + 100;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == false, "Should reject captures with too few pulses");
}

void test_rfCaptureLoop_pulse_too_short() {
    setup_test();

    // Generate RF_CAPTURE_MIN_PULSES pulses, but make the first one too short
    simulatePulse(RF_CAPTURE_MIN_PULSE_US - 10);
    for (int i = 1; i < RF_CAPTURE_MIN_PULSES; i++) {
        simulatePulse(1000);
    }

    mock_micros += RF_CAPTURE_GAP_US + 100;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == false, "Should reject captures if any of the first 16 pulses is too short");
}

void test_rfCaptureLoop_buffer_limit() {
    setup_test();

    // Generate more than RF_CAPTURE_MAX_PULSES
    for (int i = 0; i < RF_CAPTURE_MAX_PULSES + 10; i++) {
        simulatePulse(1000);
    }

    mock_micros += RF_CAPTURE_GAP_US + 100;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == true, "Should still capture up to max limit");
    ASSERT(rfCaptureGetLastLength() == RF_CAPTURE_MAX_PULSES, "Should truncate at MAX_PULSES");
}

int main() {
    test_rfCaptureLoop_happy_path();
    test_rfCaptureLoop_too_few_pulses();
    test_rfCaptureLoop_pulse_too_short();
    test_rfCaptureLoop_buffer_limit();

    printf("All RF Capture tests passed!\n");
    return 0;
}
