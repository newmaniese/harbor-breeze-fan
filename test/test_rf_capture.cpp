#include "../include/rf_capture.h"
#include <stdio.h>
#include <stdlib.h>

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
            exit(1); \
        } \
    } while (0)

// Mock variables for Arduino APIs
static unsigned long mocked_micros = 0;
static uint8_t mock_pin_mode_pin = 0;
static uint8_t mock_pin_mode_mode = 0;
static uint8_t mock_attach_interrupt_pin = 0;
static void (*mock_isr)(void) = NULL;
static int mock_attach_interrupt_mode = 0;
static int no_interrupts_count = 0;

unsigned long micros() {
    return mocked_micros;
}

void pinMode(uint8_t pin, uint8_t mode) {
    mock_pin_mode_pin = pin;
    mock_pin_mode_mode = mode;
}

void attachInterrupt(uint8_t interruptNum, void (*userFunc)(void), int mode) {
    mock_attach_interrupt_pin = interruptNum;
    mock_isr = userFunc;
    mock_attach_interrupt_mode = mode;
}

uint8_t digitalPinToInterrupt(uint8_t pin) {
    return pin; // Direct mapping for test
}

void noInterrupts() {
    no_interrupts_count++;
}

void interrupts() {
    no_interrupts_count--;
}

void reset_mocks() {
    mocked_micros = 0;
    mock_pin_mode_pin = 0;
    mock_pin_mode_mode = 0;
    mock_attach_interrupt_pin = 0;
    mock_isr = NULL;
    mock_attach_interrupt_mode = 0;
    no_interrupts_count = 0;
    rfCaptureBegin(); // Resets internal state
}

void test_rfCaptureBegin() {
    reset_mocks();

    ASSERT(mock_pin_mode_pin == 5, "Pin should be 5 (RF_RECV_PIN)");
    ASSERT(mock_pin_mode_mode == 1, "Mode should be INPUT_PULLDOWN (1)");
    ASSERT(mock_attach_interrupt_pin == 5, "Interrupt pin should be 5");
    ASSERT(mock_isr != NULL, "ISR should be attached");
    ASSERT(mock_attach_interrupt_mode == 2, "Interrupt mode should be CHANGE (2)");
}

void test_rfCapture_short_capture() {
    reset_mocks();

    // Simulate < RF_CAPTURE_MIN_PULSES pulses
    for (int i = 0; i < RF_CAPTURE_MIN_PULSES - 1; i++) {
        mocked_micros += 1000;
        mock_isr();
    }

    // Trigger gap timeout
    mocked_micros += RF_CAPTURE_GAP_US + 1;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == false, "Should not report new capture for short sequence");
    ASSERT(rfCaptureGetLastLength() == 0, "Last length should be 0");
}

void test_rfCapture_noise() {
    reset_mocks();

    // Simulate RF_CAPTURE_MIN_PULSES of very small pulses (noise)
    for (int i = 0; i < RF_CAPTURE_MIN_PULSES + 5; i++) {
        mocked_micros += 10; // Less than RF_CAPTURE_MIN_PULSE_US (80)
        mock_isr();
    }

    // Trigger gap timeout
    mocked_micros += RF_CAPTURE_GAP_US + 1;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == false, "Should not report new capture for noise");
    ASSERT(rfCaptureGetLastLength() == 0, "Last length should be 0");
}

void test_rfCapture_valid_sequence() {
    reset_mocks();

    uint32_t initial_seq = rfCaptureGetLastSeq();

    // Need to trigger the first ISR to start capturing
    mock_isr();

    // Simulate a valid sequence
    for (int i = 0; i < RF_CAPTURE_MIN_PULSES + 5; i++) {
        mocked_micros += 500; // Valid pulse duration
        mock_isr();
    }

    // Trigger gap timeout
    mocked_micros += RF_CAPTURE_GAP_US + 1;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == true, "Should report new capture");
    ASSERT(rfCaptureGetLastLength() == RF_CAPTURE_MIN_PULSES + 5, "Last length should match captured pulses");
    ASSERT(rfCaptureGetLastSeq() == initial_seq + 1, "Sequence number should increment");

    uint16_t out[100];
    int count = rfCaptureGetLastPulses(out, 100);
    ASSERT(count == RF_CAPTURE_MIN_PULSES + 5, "Should return correct pulse count");
    ASSERT(out[0] == 500, "First pulse should be 500us");

    rfCaptureClearNew();
    ASSERT(rfCaptureHasNew() == false, "Should clear new flag");
}

void test_rfCapture_overflow() {
    reset_mocks();

    // Need to trigger the first ISR to start capturing
    mock_isr();

    // Simulate more than RF_CAPTURE_MAX_PULSES pulses
    for (int i = 0; i < RF_CAPTURE_MAX_PULSES + 10; i++) {
        mocked_micros += 500;
        mock_isr();
    }

    // Trigger gap timeout
    mocked_micros += RF_CAPTURE_GAP_US + 1;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == true, "Should report new capture");
    ASSERT(rfCaptureGetLastLength() == RF_CAPTURE_MAX_PULSES, "Last length should be capped at MAX_PULSES");
}

void test_rfCapture_max_duration() {
    reset_mocks();

    // Trigger first edge to start capture and set lastEdgeMicros
    mock_isr();

    // Simulate a huge delta > 0xFFFF
    mocked_micros += 100000;
    mock_isr();

    // A few normal pulses to make it a valid capture
    for (int i = 0; i < RF_CAPTURE_MIN_PULSES; i++) {
        mocked_micros += 500;
        mock_isr();
    }

    // Trigger gap timeout
    mocked_micros += RF_CAPTURE_GAP_US + 1;
    rfCaptureLoop();

    ASSERT(rfCaptureHasNew() == true, "Should report new capture");

    uint16_t out[100];
    rfCaptureGetLastPulses(out, 100);
    ASSERT(out[0] == 0xFFFF, "Huge duration should be capped at 0xFFFF");
    ASSERT(out[1] == 500, "Normal duration should be recorded");
}

int main() {
    test_rfCaptureBegin();
    test_rfCapture_short_capture();
    test_rfCapture_noise();
    test_rfCapture_valid_sequence();
    test_rfCapture_overflow();
    test_rfCapture_max_duration();

    printf("All rf_capture tests passed!\n");
    return 0;
}
