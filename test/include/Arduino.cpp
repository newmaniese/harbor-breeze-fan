#include "Arduino.h"

unsigned long _mock_micros = 0;

unsigned long micros() {
    return _mock_micros;
}

void pinMode(uint8_t pin, uint8_t mode) {}
void attachInterrupt(uint8_t pin, void (*isr)(void), int mode) {}
uint8_t digitalPinToInterrupt(uint8_t pin) { return pin; }
void noInterrupts() {}
void interrupts() {}
