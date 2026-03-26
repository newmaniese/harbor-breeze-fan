#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define IRAM_ATTR

extern unsigned long _mock_micros;
unsigned long micros();

void pinMode(uint8_t pin, uint8_t mode);
void attachInterrupt(uint8_t pin, void (*isr)(void), int mode);
uint8_t digitalPinToInterrupt(uint8_t pin);
void noInterrupts();
void interrupts();

#define INPUT_PULLDOWN 0x09
#define CHANGE 1
