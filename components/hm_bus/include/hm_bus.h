/*
 * RS485 transport for the Heatmiser V3 bus: one request, one reply, with
 * timing captured for bring-up. Thread safe; transactions are serialised.
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "hm_frame.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int      uart_num;
    int      tx_gpio;
    int      rx_gpio;
    int      baud;
    uint8_t  master_addr;
    uint32_t reply_timeout_ms;
    uint32_t interbyte_timeout_ms;
    uint32_t gap_ms;
    bool     edge_timing;
} hm_bus_config_t;

#define HM_BUS_CONFIG_DEFAULT() {                                  \
    .uart_num = CONFIG_HM_BUS_UART_NUM,                            \
    .tx_gpio = CONFIG_HM_BUS_TX_GPIO,                              \
    .rx_gpio = CONFIG_HM_BUS_RX_GPIO,                              \
    .baud = CONFIG_HM_BUS_BAUD,                                    \
    .master_addr = CONFIG_HM_BUS_MASTER_ADDR,                      \
    .reply_timeout_ms = CONFIG_HM_BUS_REPLY_TIMEOUT_MS,            \
    .interbyte_timeout_ms = CONFIG_HM_BUS_INTERBYTE_TIMEOUT_MS,    \
    .gap_ms = CONFIG_HM_BUS_GAP_MS,                                \
    .edge_timing = HM_BUS_EDGE_TIMING_DEFAULT,                     \
}
#ifdef CONFIG_HM_BUS_EDGE_TIMING
#define HM_BUS_EDGE_TIMING_DEFAULT true
#else
#define HM_BUS_EDGE_TIMING_DEFAULT false
#endif

typedef enum {
    HM_TXN_OK = 0,
    HM_TXN_TIMEOUT,    /* nothing came back */
    HM_TXN_PARTIAL,    /* reply started, then stopped short of its length */
    HM_TXN_BAD_FRAME,  /* full frame received but invalid; see proto */
    HM_TXN_ECHO_ONLY,  /* only our own request came back */
    HM_TXN_ERR,        /* driver or argument error */
} hm_txn_result_t;

typedef struct {
    hm_txn_result_t result;
    hm_status_t     proto;          /* parse status when a frame arrived */
    uint8_t         req[HM_MAX_FRAME];
    size_t          req_len;
    uint8_t         rx[HM_MAX_FRAME];
    size_t          rx_len;         /* reply bytes, echo removed */
    hm_reply_t      reply;          /* valid when result == HM_TXN_OK */
    bool            echo_seen;      /* transceiver looped our request back */
    uint32_t        uart_errors;    /* framing, parity, overflow during txn */

    /* Timestamps (esp_timer, us) and derived timings. 0 = not observed. */
    int64_t  t_tx_start_us;
    int64_t  t_tx_done_us;          /* last stop bit of the request left */
    int64_t  t_rx_edge_us;          /* first start bit of the reply (edge ISR) */
    int64_t  t_rx_first_us;         /* first reply byte handed over by the driver */
    int64_t  t_rx_last_us;          /* last reply byte handed over */
    int32_t  turnaround_us;         /* request end -> reply start bit; -1 unknown */
    bool     turnaround_from_edge;  /* true: from edge ISR; false: from byte arrival */
} hm_txn_t;

typedef struct {
    uint32_t txns, ok, timeout, partial, crc, bad_other, echo_only, uart_errors;
} hm_bus_counters_t;

esp_err_t hm_bus_init(const hm_bus_config_t *cfg);
const hm_bus_config_t *hm_bus_config(void);
void hm_bus_set_master(uint8_t addr);
void hm_bus_set_timing(uint32_t reply_timeout_ms, uint32_t gap_ms);

/* Move the UART to other TX/RX pins at run time (for bench checks). */
esp_err_t hm_bus_set_pins(int tx_gpio, int rx_gpio);

/* Send a pre-built request and collect one reply. expect_stat is the source
 * address the reply must carry (0 = any). Waits the bus gap before returning. */
esp_err_t hm_bus_transact(const uint8_t *req, size_t req_len, uint8_t expect_stat, hm_txn_t *t);

/* The same with a reply timeout for this transaction only (0 = configured). */
esp_err_t hm_bus_transact_timeout(const uint8_t *req, size_t req_len, uint8_t expect_stat,
                                  uint32_t reply_timeout_ms, hm_txn_t *t);

/* Read `count` bytes from unique address `start` (HM_READ_ALL for the DCB). */
esp_err_t hm_bus_read(uint8_t stat, uint16_t start, uint16_t count, hm_txn_t *t);

/* Passive capture: collect bytes for up to timeout_ms, ending early after
 * `silence_ms` of quiet once something arrived. Does not transmit. Returns
 * bytes captured; *t_first_us gets the arrival time of the first chunk. */
size_t hm_bus_sniff(uint8_t *buf, size_t cap, uint32_t timeout_ms, uint32_t silence_ms,
                    int64_t *t_first_us, uint32_t *uart_errors);

void hm_bus_counters(hm_bus_counters_t *out);
void hm_bus_counters_reset(void);

const char *hm_txn_result_str(hm_txn_result_t r);

#ifdef __cplusplus
}
#endif
