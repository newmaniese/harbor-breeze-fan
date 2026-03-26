#include <stdio.h>
#include <assert.h>
#include <string.h>
#include "../include/harbor_breeze.h"

void test_harborBreezeHubDecodePulses_null_pulses() {
    char symbols[100];
    const char* matched_cmd;
    int res = harborBreezeHubDecodePulses(NULL, 100, symbols, sizeof(symbols), &matched_cmd);
    assert(res == 0);
    printf("test_harborBreezeHubDecodePulses_null_pulses passed\n");
}

void test_harborBreezeHubDecodePulses_short_len() {
    uint16_t pulses[40] = {0};
    char symbols[100];
    const char* matched_cmd;
    int res = harborBreezeHubDecodePulses(pulses, 40, symbols, sizeof(symbols), &matched_cmd);
    assert(res == 0);
    printf("test_harborBreezeHubDecodePulses_short_len passed\n");
}

void test_harborBreezeHubDecodePulses_null_symbols() {
    uint16_t pulses[100] = {0};
    const char* matched_cmd;
    int res = harborBreezeHubDecodePulses(pulses, 100, NULL, 100, &matched_cmd);
    assert(res == 0);
    printf("test_harborBreezeHubDecodePulses_null_symbols passed\n");
}

void test_harborBreezeHubDecodePulses_short_buf_sz() {
    uint16_t pulses[100] = {0};
    char symbols[50];
    const char* matched_cmd;
    int res = harborBreezeHubDecodePulses(pulses, 100, symbols, 50, &matched_cmd);
    assert(res == 0);
    printf("test_harborBreezeHubDecodePulses_short_buf_sz passed\n");
}

int main() {
    test_harborBreezeHubDecodePulses_null_pulses();
    test_harborBreezeHubDecodePulses_short_len();
    test_harborBreezeHubDecodePulses_null_symbols();
    test_harborBreezeHubDecodePulses_short_buf_sz();
    printf("All tests passed!\n");
    return 0;
}
