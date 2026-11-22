/* Display glue: the UI console and the saved screen timeout. */
#pragma once
#include <stdint.h>

void ui_register_commands(void);

/* Load the saved screen timeout. */
uint32_t display_saved_blank_timeout(void);
