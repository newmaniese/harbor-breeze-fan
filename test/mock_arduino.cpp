#include "Arduino.h"

unsigned long mock_micros = 0;

unsigned long micros() {
    return mock_micros;
}

void pinMode(int pin, int mode) {
    (void)pin;
    (void)mode;
}

int digitalPinToInterrupt(int pin) {
    return pin;
}

voidFuncPtr mock_isr = NULL;

void attachInterrupt(int interruptNum, voidFuncPtr userFunc, int mode) {
    (void)interruptNum;
    (void)mode;
    mock_isr = userFunc;
}

void noInterrupts() {}
void interrupts() {}
