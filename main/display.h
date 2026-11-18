/* Milestone 3 glue: the display soak's load generator and the UI console. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Start or stop the soak load: an NVS write every 30 s, a ping to the
 * gateway every second, and a DSOAK status line every 10 minutes. */
void display_soak(bool on);
bool display_soak_running(void);
void display_soak_log(const char *why);
void display_soak_drift_seen(void);

void ui_register_commands(void);

/* Load the saved screen timeout. */
uint32_t display_saved_blank_timeout(void);
