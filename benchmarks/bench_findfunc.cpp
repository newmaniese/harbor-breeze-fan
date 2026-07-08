#include <iostream>
#include <chrono>
#include <cstring>
#include <vector>

// Dummy defines to match main.cpp
#define HB_LIGHT_T "01000010"
#define HB_LIGHT_D "01000001"
#define HB_FAN_FST "10000000"
#define HB_FAN_FS1 "00000001"
#define HB_FAN_FS2 "00000010"
#define HB_FAN_FS3 "00000011"
#define HB_FAN_FS4 "00000100"
#define HB_FAN_FS5 "00000101"
#define HB_FAN_FS6 "00000110"
#define HB_FAN_FSD "10000001"
#define HB_FAN_FWD "10000010"
#define HB_FAN_FSN "10000100"
#define HB_DELAY_O "00010000"
#define HB_DELAY_2 "00100000"
#define HB_DELAY_4 "01000000"
#define HB_DELAY_8 "10000000" // actually this overlaps with FAN_FST in my dummy, whatever
#define HB_LIGHT_H "00001000"

struct Command {
  const char* name;
  const char* func;
};

// UNSORTED (original)
static const Command commands[] = {
  { "light_toggle",    HB_LIGHT_T },
  { "light_dim",       HB_LIGHT_D },
  { "fan_off",         HB_FAN_FST },
  { "fan_speed_1",     HB_FAN_FS1 },
  { "fan_speed_2",     HB_FAN_FS2 },
  { "fan_speed_3",     HB_FAN_FS3 },
  { "fan_speed_4",     HB_FAN_FS4 },
  { "fan_speed_5",     HB_FAN_FS5 },
  { "fan_speed_6",     HB_FAN_FS6 },
  { "fan_direction_summer", HB_FAN_FSD },
  { "fan_direction_winter", HB_FAN_FWD },
  { "nature_breeze",   HB_FAN_FSN },
  { "delay_off",       HB_DELAY_O },
  { "delay_2h",        HB_DELAY_2 },
  { "delay_4h",        HB_DELAY_4 },
  { "delay_8h",        HB_DELAY_8 },
  { "home_shield",     HB_LIGHT_H },
};
static const int numCommands = sizeof(commands) / sizeof(commands[0]);

static const char* findFunc(const char* cmd) {
  for (int i = 0; i < numCommands; i++)
    if (strcmp(commands[i].name, cmd) == 0)
      return commands[i].func;
  return nullptr;
}

int main() {
    std::vector<const char*> search_cmds = {
        "light_toggle", "fan_speed_6", "delay_8h", "home_shield", "not_found",
        "fan_direction_summer", "nature_breeze", "delay_2h", "fan_speed_3", "light_dim"
    };

    // Warm up
    volatile const char* dummy;
    for (int i = 0; i < 10000; i++) {
        for (auto cmd : search_cmds) {
            dummy = findFunc(cmd);
        }
    }

    auto start = std::chrono::high_resolution_clock::now();
    int iterations = 1000000;
    for (int i = 0; i < iterations; i++) {
        for (auto cmd : search_cmds) {
            dummy = findFunc(cmd);
        }
    }
    auto end = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> diff = end - start;

    std::cout << "Baseline Time: " << diff.count() << " s" << std::endl;
    return 0;
}
