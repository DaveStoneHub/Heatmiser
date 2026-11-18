#pragma once
#include <stdint.h>
#include "hm_bus.h"

void bench_register_commands(void);
void soak_register_command(void);
void poll_register_commands(void);

/* Shared helpers */
void bench_print_hex(const char *label, const uint8_t *b, size_t n);
void bench_print_txn(const hm_txn_t *t);
int  bench_parse_int(const char *s, int dflt, int lo, int hi, int *out);
