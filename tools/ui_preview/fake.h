#pragma once
#include <stdbool.h>
#include <stdint.h>

void fake_load_house(void);
void fake_clear(void);
void fake_set_scan(bool running, uint8_t pass, uint8_t next, uint8_t found);
void fake_set_boost(uint8_t addr, unsigned left);
void fake_pending_set(uint8_t addr, int c);
void fake_notice(const char *msg);
int png_write(const char *path, const uint8_t *rgb, int w, int h);
