#pragma once
#include <stdint.h>
#include <stddef.h>

#define INPUT_PULLDOWN 1
#define CHANGE 2
#define IRAM_ATTR

typedef void (*voidFuncPtr)(void);

unsigned long micros();
void pinMode(int pin, int mode);
int digitalPinToInterrupt(int pin);
void attachInterrupt(int interruptNum, voidFuncPtr userFunc, int mode);

void noInterrupts();
void interrupts();

extern unsigned long mock_micros;
extern voidFuncPtr mock_isr;
#include <stdio.h>
