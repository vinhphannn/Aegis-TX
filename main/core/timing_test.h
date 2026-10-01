/**
 * timing_test.h - Aegis-TX System Health Monitor
 * ============================================
 * Không cần phần cứng đặc biệt. Chỉ cần mở Serial Monitor (115200 baud).
 *
 * CÁCH SỬ DỤNG:
 *   1. #include "timing_test.h" vào bất kỳ task nào muốn đo
 *   2. Gọi TIMING_MARK_START(id) trước đoạn code cần đo
 *   3. Gọi TIMING_MARK_END(id) sau đoạn code đó
 *   4. Gọi TIMING_REPORT() định kỳ để xem kết quả
 *
 * OUTPUT MẪU (Serial Monitor):
 *   [TIMING] sensor_task  : avg=2.31ms  max=2.89ms  miss=0
 *   [TIMING] radio_tx     : avg=1.12ms  max=1.78ms  miss=0
 *   [TIMING] ui_render    : avg=8.50ms  max=12.3ms  miss=2
 */

#pragma once
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"

// Số lượng điểm đo tối đa
#define TIMING_MAX_MARKERS  8

typedef struct {
    const char *name;
    int64_t     start_us;
    int64_t     sum_us;
    int64_t     max_us;
    uint32_t    count;
    uint32_t    miss;  // Số lần vượt ngưỡng
    int64_t     budget_us; // Ngưỡng cảnh báo
} TimingMarker_t;

// Mảng marker tĩnh - đủ dùng cho toàn hệ thống
static TimingMarker_t _timing_markers[TIMING_MAX_MARKERS] = {0};
static int _timing_count = 0;

// Đăng ký một điểm đo mới (gọi 1 lần khi khởi động task)
static inline int timing_register(const char *name, int64_t budget_us)
{
    if (_timing_count >= TIMING_MAX_MARKERS) return -1;
    int id = _timing_count++;
    _timing_markers[id].name      = name;
    _timing_markers[id].budget_us = budget_us;
    return id;
}

// Đánh dấu bắt đầu
static inline void timing_start(int id)
{
    if (id < 0 || id >= _timing_count) return;
    _timing_markers[id].start_us = esp_timer_get_time();
}

// Đánh dấu kết thúc
static inline void timing_end(int id)
{
    if (id < 0 || id >= _timing_count) return;
    int64_t elapsed = esp_timer_get_time() - _timing_markers[id].start_us;
    _timing_markers[id].sum_us += elapsed;
    _timing_markers[id].count++;
    if (elapsed > _timing_markers[id].max_us) {
        _timing_markers[id].max_us = elapsed;
    }
    if (_timing_markers[id].budget_us > 0 && elapsed > _timing_markers[id].budget_us) {
        _timing_markers[id].miss++;
    }
}

// In báo cáo (gọi định kỳ, ví dụ mỗi 5 giây)
static inline void timing_report(void)
{
    esp_log_write(ESP_LOG_INFO, "TIMING", "\n");
    esp_log_write(ESP_LOG_INFO, "TIMING", "╔══ Aegis-TX TIMING REPORT ══════════════════════════╗\n");
    for (int i = 0; i < _timing_count; i++) {
        TimingMarker_t *m = &_timing_markers[i];
        if (m->count == 0) continue;
        int64_t avg_us = m->sum_us / m->count;
        const char *status = (m->miss > 0) ? "⚠ OVERRUN" : "✓ OK";
        esp_log_write(ESP_LOG_INFO, "TIMING",
            "║ %-14s avg=%4lldµs max=%4lldµs miss=%3lu %s\n",
            m->name, avg_us, m->max_us, m->miss, status);
        // Reset sau mỗi lần report
        m->sum_us = 0; m->max_us = 0; m->count = 0; m->miss = 0;
    }
    esp_log_write(ESP_LOG_INFO, "TIMING", "╚═════════════════════════════════════════════════╝\n");
}

// Macro tiện lợi
#define TIMING_REGISTER(name, budget_us) timing_register(name, budget_us)
#define TIMING_START(id)                 timing_start(id)
#define TIMING_END(id)                   timing_end(id)
#define TIMING_REPORT()                  timing_report()
