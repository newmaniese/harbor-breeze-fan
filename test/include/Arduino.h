#pragma once
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

#define IRAM_ATTR
#define INPUT_PULLDOWN 1
#define CHANGE 2

unsigned long micros();
void pinMode(uint8_t pin, uint8_t mode);
void attachInterrupt(uint8_t interruptNum, void (*userFunc)(void), int mode);
uint8_t digitalPinToInterrupt(uint8_t pin);
void noInterrupts();
void interrupts();
