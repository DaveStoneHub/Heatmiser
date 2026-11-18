/*
 * LVGL's heap in PSRAM (CONFIG_LV_USE_CUSTOM_MALLOC). Every widget, style and
 * label text lives here instead of in internal RAM, which Wi-Fi, BLE and
 * Matter need (design doc, "Memory and flash"). Falls back to internal RAM
 * only if PSRAM is full.
 */
#include "esp_heap_caps.h"
#include "lvgl.h"

#if LV_USE_STDLIB_MALLOC == LV_STDLIB_CUSTOM

#define CAPS_PSRAM    (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define CAPS_FALLBACK (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)

void lv_mem_init(void)
{
}

void lv_mem_deinit(void)
{
}

lv_mem_pool_t lv_mem_add_pool(void *mem, size_t bytes)
{
    LV_UNUSED(mem);
    LV_UNUSED(bytes);
    return NULL;
}

void lv_mem_remove_pool(lv_mem_pool_t pool)
{
    LV_UNUSED(pool);
}

void *lv_malloc_core(size_t size)
{
    void *p = heap_caps_malloc(size, CAPS_PSRAM);
    return p ? p : heap_caps_malloc(size, CAPS_FALLBACK);
}

void *lv_realloc_core(void *p, size_t new_size)
{
    void *q = heap_caps_realloc(p, new_size, CAPS_PSRAM);
    return q ? q : heap_caps_realloc(p, new_size, CAPS_FALLBACK);
}

void lv_free_core(void *p)
{
    heap_caps_free(p);
}

void lv_mem_monitor_core(lv_mem_monitor_t *mon)
{
    multi_heap_info_t info;
    heap_caps_get_info(&info, CAPS_PSRAM);
    mon->total_size = info.total_free_bytes + info.total_allocated_bytes;
    mon->free_size = info.total_free_bytes;
    mon->free_biggest_size = info.largest_free_block;
    mon->max_used = info.total_free_bytes + info.total_allocated_bytes - info.minimum_free_bytes;
    mon->used_cnt = info.allocated_blocks;
    mon->free_cnt = info.free_blocks;
    mon->used_pct = mon->total_size ? (uint8_t)(100 - 100ULL * mon->free_size / mon->total_size) : 0;
    mon->frag_pct = 0;
}

lv_result_t lv_mem_test_core(void)
{
    return heap_caps_check_integrity(CAPS_PSRAM, false) ? LV_RESULT_OK : LV_RESULT_INVALID;
}

#endif
