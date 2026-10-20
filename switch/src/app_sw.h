#ifndef app_sw_h_
#define app_sw_h_

#include <stdbool.h>

// 2-channel relay module wiring: IN1 -> GPIO4 (IN2 is not used)
#define RELAY_PIN 4

// Logic level on IN1 that switches the relay ON.
// Most 2-channel relay modules are ACTIVE-LOW: LOW (0) = relay ON, HIGH (1) = relay OFF.
// If your relay behaves backwards (ON when it should be OFF), change this to 1.
#define RELAY_ON_LEVEL 0
#define RELAY_OFF_LEVEL (!RELAY_ON_LEVEL)

void init_hw(void);
void switch_set(bool);
bool switch_get(void);

#endif