/* Milestone 6: the hourly health line and the health, ota and pin commands. */
#pragma once
#include <stdbool.h>

void health_register_commands(void);
void health_print(void);
/* For the trial of a new firmware: the bus works. */
bool health_ok(void);
