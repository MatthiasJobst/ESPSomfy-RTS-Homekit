#pragma once
#include <cstdint>
#include <cstddef>
#include "esp_err.h"

// ── RMT types (minimal host stubs) ──────────────────────────────────────────

typedef void *rmt_channel_handle_t;
typedef int   rmt_gpio_num_t;

typedef enum {
    RMT_CLK_SRC_DEFAULT = 0,
} rmt_clock_source_t;

typedef enum {
    RMT_ENCODING_RESET    = 0,
    RMT_ENCODING_COMPLETE = (1 << 0),
    RMT_ENCODING_MEM_FULL = (1 << 1),
} rmt_encode_state_t;

typedef struct {
    uint16_t duration0 : 15;
    uint16_t level0    : 1;
    uint16_t duration1 : 15;
    uint16_t level1    : 1;
} rmt_symbol_word_t;

// rmt_encoder_t must be declared before rmt_encoder_handle_t so the typedef works.
struct rmt_encoder_t {
    size_t    (*encode)(rmt_encoder_t *, rmt_channel_handle_t,
                        const void *, size_t, rmt_encode_state_t *);
    esp_err_t (*reset)(rmt_encoder_t *);
    esp_err_t (*del)(rmt_encoder_t *);
};

// In real ESP-IDF, rmt_encoder_handle_t is rmt_encoder_t*.
typedef rmt_encoder_t *rmt_encoder_handle_t;

typedef struct {
    int      gpio_num;
    rmt_clock_source_t clk_src;
    uint32_t resolution_hz;
    size_t   mem_block_symbols;
    size_t   trans_queue_depth;
    struct { uint8_t with_dma : 1; } flags;
} rmt_tx_channel_config_t;

typedef struct {
    int loop_count;
    struct {
        uint8_t eot_level         : 1;
        uint8_t queue_nonblocking : 1;
    } flags;
} rmt_transmit_config_t;

typedef struct {
    uint32_t dummy;
} rmt_tx_done_event_data_t;

typedef bool (*rmt_tx_done_callback_t)(rmt_channel_handle_t,
                                       const rmt_tx_done_event_data_t *,
                                       void *);

typedef struct {
    rmt_tx_done_callback_t on_trans_done;
} rmt_tx_event_callbacks_t;

typedef struct { } rmt_copy_encoder_config_t;

// Captured done callback — tests can fire it directly.
extern rmt_tx_done_callback_t rmt_stub_done_cb;
extern void                  *rmt_stub_done_cb_ctx;

// Captured rmt_transmit() calls — tests inspect payload pointers and sizes.
#define RMT_STUB_QUEUE_MAX 16
struct rmt_stub_queued_t { const void *data; size_t size; };
extern rmt_stub_queued_t rmt_stub_queue[RMT_STUB_QUEUE_MAX];
extern int               rmt_stub_queue_count;

// Last encoder passed to rmt_transmit — tests invoke encode/reset via function pointers.
extern rmt_encoder_handle_t rmt_stub_last_encoder;

// Set > 0 to make rmt_transmit return ESP_FAIL after this many successes (then reset to 0).
extern int rmt_stub_fail_after;

// Stub copy encoder: encode is a no-op returning COMPLETE, reset is a no-op.
inline size_t    rmt_stub_copy_encode(rmt_encoder_t *, rmt_channel_handle_t,
                                      const void *, size_t,
                                      rmt_encode_state_t *s) {
    *s = RMT_ENCODING_COMPLETE; return 1;
}
inline esp_err_t rmt_stub_copy_reset(rmt_encoder_t *) { return ESP_OK; }
inline esp_err_t rmt_stub_copy_del(rmt_encoder_t *)   { return ESP_OK; }

// Static stub encoder instance returned by rmt_new_copy_encoder().
inline rmt_encoder_t &rmt_stub_copy_encoder_instance() {
    static rmt_encoder_t inst = { rmt_stub_copy_encode,
                                  rmt_stub_copy_reset,
                                  rmt_stub_copy_del };
    return inst;
}

inline esp_err_t rmt_new_tx_channel(const rmt_tx_channel_config_t *,
                                    rmt_channel_handle_t *out) {
    static int dummy = 1;
    *out = &dummy;
    return ESP_OK;
}
inline esp_err_t rmt_new_copy_encoder(const rmt_copy_encoder_config_t *,
                                      rmt_encoder_handle_t *out) {
    *out = &rmt_stub_copy_encoder_instance();
    return ESP_OK;
}
inline esp_err_t rmt_tx_register_event_callbacks(rmt_channel_handle_t,
                                                  const rmt_tx_event_callbacks_t *cbs,
                                                  void *ctx) {
    if (cbs) {
        rmt_stub_done_cb     = cbs->on_trans_done;
        rmt_stub_done_cb_ctx = ctx;
    }
    return ESP_OK;
}
inline esp_err_t rmt_enable(rmt_channel_handle_t)               { return ESP_OK; }
inline esp_err_t rmt_disable(rmt_channel_handle_t)              { return ESP_OK; }
inline esp_err_t rmt_del_channel(rmt_channel_handle_t)          { return ESP_OK; }
inline esp_err_t rmt_del_encoder(rmt_encoder_handle_t enc) {
    if (enc && enc->del) return enc->del(enc);
    return ESP_OK;
}
inline esp_err_t rmt_tx_wait_all_done(rmt_channel_handle_t, int){ return ESP_OK; }
inline esp_err_t rmt_transmit(rmt_channel_handle_t,
                               rmt_encoder_handle_t encoder,
                               const void *payload, size_t size,
                               const rmt_transmit_config_t *) {
    rmt_stub_last_encoder = encoder;
    if (rmt_stub_fail_after > 0) {
        rmt_stub_fail_after--;
        if (rmt_stub_fail_after == 0) { rmt_stub_fail_after = -1; return ESP_FAIL; }
    } else if (rmt_stub_fail_after < 0) {
        return ESP_FAIL;
    }
    if (rmt_stub_queue_count < RMT_STUB_QUEUE_MAX)
        rmt_stub_queue[rmt_stub_queue_count++] = { payload, size };
    return ESP_OK;
}
