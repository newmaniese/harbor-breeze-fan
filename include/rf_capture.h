#ifndef RF_CAPTURE_H
#define RF_CAPTURE_H

#include <Arduino.h>

// Max pulse count per capture (alternating high/low durations in microseconds).
#define RF_CAPTURE_MAX_PULSES 512
// Gap with no edge (microseconds) to consider capture complete.
// Increased from 5000 to 15000 to capture full 6-repeat frames (8ms gap = 8000µs between repeats)
#define RF_CAPTURE_GAP_US 15000
// Ignore captures shorter than this (noise); only report and store if pulse count >= this.
#define RF_CAPTURE_MIN_PULSES 20
// Ignore captures where pulse widths are all tiny (floating pin / electrical noise). Real 315 MHz OOK uses ~100+ µs.
#define RF_CAPTURE_MIN_PULSE_US 80

// Call once from setup() to init GPIO and attach interrupt.
void rfCaptureBegin();

// Call every loop(); detects gap timeout, copies completed capture to "last", resets for next.
void rfCaptureLoop();

// Last received sequence (valid after a capture completed). Length in pulse count.
int rfCaptureGetLastLength();
// Copy last pulse durations (microseconds) into out; maxLen = max entries to copy. Returns count copied.
int rfCaptureGetLastPulses(uint16_t* out, int maxLen);
// Sequence number incremented on each new completed capture; for clients to detect changes.
uint32_t rfCaptureGetLastSeq();

// True when a new capture completed and not yet consumed (e.g. for WebSocket broadcast).
bool rfCaptureHasNew();
void rfCaptureClearNew();

#endif // RF_CAPTURE_H
