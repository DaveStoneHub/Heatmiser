/* Shared test helpers for the host tests. */
#pragma once
#include <stdio.h>

extern int g_fail, g_pass;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define CHECK_EQ(a, b) do { \
    long long _a = (long long)(a), _b = (long long)(b); \
    if (_a == _b) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "%s:%d: %s == %s failed: 0x%llX vs 0x%llX\n", \
                             __FILE__, __LINE__, #a, #b, _a, _b); } \
} while (0)

void test_write_all(void);
void test_sched_all(void);
void test_zone_all(void);
