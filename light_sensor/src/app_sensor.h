#ifndef app_sensor_h_
#define app_sensor_h_

#include <stdbool.h>

// LDR module wiring: DO -> GPIO34
#define LDR_DO_PIN 34

// Logic level the module's DO pin outputs when it is DARK.
// Most LM393 LDR modules output HIGH (1) in the dark, LOW (0) in the light.
// If your readings are inverted, change this to 0.
#define LDR_DARK_LEVEL 1

typedef void (*light_changed_f)(bool);
void init_hw(light_changed_f);
bool is_light_low(void);

#endif