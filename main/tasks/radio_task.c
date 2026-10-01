/**
 * radio_task.c - Aegis-TX Radio Control Task (ELRS / CRSF Version)
 * ==========================================================
 * ExpressLRS (CRSF Protocol).
 * Tốc độ baud: 420,000 bps.
 * Chu kỳ gửi: 4ms (250Hz) cố định.
 */

#include "core/sys_state.h"
#include "core/sys_config.h"
#include "core/crsf.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "ble_hid.h"
#include <string.h>

static const char *TAG = "RADIO_TASK_ELRS";

void radio_task(void *pvParameters)
{
    // Snapshot channels hợp lệ gần nhất để Failsafe
    SystemState_t last_valid = {0};
    last_valid.channels[0]  = 1000;  // Throttle thấp nhất
    for (int i = 1; i < MAX_CHANNELS; i++) last_valid.channels[i] = 1500;
    
    // Khởi tạo UART cho CRSF
    crsf_init();
    ESP_LOGI(TAG, "CRSF UART Initialized at 420kbps.");

    vTaskDelay(pdMS_TO_TICKS(500));
    ESP_LOGI(TAG, "ELRS Radio Task Started (250Hz)");

    TickType_t last_wake = xTaskGetTickCount();

    // QUAN TRỌNG: Khai báo ngoài vòng lặp để tránh static-inside-loop corruption
    TelemetryData_t local_telem;
    memset(&local_telem, 0, sizeof(local_telem));

    while (1) {
        
        // Đọc Snapshot từ g_state trước
        SystemState_t snap;
        if (xSemaphoreTake(sys_mutex, pdMS_TO_TICKS(2)) == pdTRUE) {
            snap = g_state;
            
            // Đẩy Telemetry mới nhận được vào biến toàn cục (chỉ nếu đang bay)
            if (snap.sys_mode != 2) {
                g_state.telemetry = local_telem;
            }
            
            xSemaphoreGive(sys_mutex);
            
            // Cập nhật bản sao hợp lệ
            if (snap.channels[0] >= 1000 && snap.channels[0] <= 2000) {
                last_valid = snap;
            }
        } else {
            // Failsafe: Dùng lại dữ liệu cũ nếu mutex bị kẹt
            snap = last_valid;
        }

        static uint8_t last_sys_mode = 0;
        
        // Quản lý chuyển đổi chế độ hệ thống (Flight <-> Sim)
        if (snap.sys_mode != last_sys_mode) {
            if (snap.sys_mode == 2) {
                ESP_LOGI(TAG, "Entering Simulator Mode. Turning OFF CRSF, Starting BLE.");
                crsf_set_power(false);
                ble_hid_init();
            } else {
                if (last_sys_mode == 2) {
                    ESP_LOGI(TAG, "Exiting Simulator Mode. Turning ON CRSF, Stopping BLE.");
                    ble_hid_deinit();
                }
                crsf_set_power(true);
            }
            last_sys_mode = snap.sys_mode;
        }

        // B3: Chế độ SIMULATOR
        if (snap.sys_mode == 2) {
            // Đóng gói nút bấm (Hỗ trợ tối đa 16 nút)
            uint16_t buttons = 0;
            if (snap.channels[4] > 1500) buttons |= (1 << 0); // ARM (Ch5)
            if (snap.channels[5] > 1500) buttons |= (1 << 1); // Beeper (Ch6)
            if (snap.channels[6] > 1500) buttons |= (1 << 2); // LED (Ch7)
            if (snap.channels[7] > 1500) buttons |= (1 << 3); // AUX (Ch8)
            
            // Công tắc 3 nấc Flight Mode (Ch9) -> 3 nút bấm ảo riêng biệt
            if (snap.channels[8] < 1200) buttons |= (1 << 4); // LOW
            if (snap.channels[8] > 1300 && snap.channels[8] < 1700) buttons |= (1 << 5); // MID
            if (snap.channels[8] > 1800) buttons |= (1 << 6); // HIGH
            
            // Bắn dữ liệu Joystick sang Bluetooth
            ble_hid_send_report(
                snap.channels[0], // Throttle
                snap.channels[1], // Yaw
                snap.channels[2], // Pitch
                snap.channels[3], // Roll
                snap.channels[10], // P1
                snap.channels[11], // P2
                buttons
            );
            
            // Tạm dừng 10ms (100Hz là quá đủ mượt cho Simulator)
            vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(10));
            continue;
        }

        // B1: Đọc Telemetry từ UART TRƯỚC (luôn phải đọc trước khi push vào g_state)
        crsf_receive_telemetry(&local_telem);

        // Timeout Telemetry: 2 điều kiện để phát hiện mất kết nối:
        // (1) Quá 1 giây không có gói tin mới nào
        // (2) ELRS báo LQ = 0 (RF link drop) - ELRS set LQ=0 khi mất sóng chính xác hơn timeout
        bool lq_lost = (local_telem.link_quality == 0 && local_telem.link_active);
        bool timed_out = ((esp_timer_get_time() / 1000) - local_telem.last_recv_ms > 1000);
        if (timed_out || lq_lost) {
            local_telem.link_active  = false;
            local_telem.rssi         = 0;
            local_telem.snr          = 0;
            local_telem.link_quality = 0;
        }

        // B4: Đóng gói và gửi CRSF RC Channels
        // ─────────────────────────────────────────────────────────
        // Layout cố định (KHÔNG thay đổi thứ tự mà không cập nhật QGC):
        //   Ch1-4  : Throttle / Yaw / Pitch / Roll  (joystick)
        //   Ch5-8  : 4 Công tắc 2 nấc (ARM, Beeper, LED, AUX)
        //   Ch9-10 : 2 Công tắc 3 nấc (Flight Mode, Sys Mode)
        //   Ch11-12: 2 Biến trở (P1, P2)
        //   Ch13-16: 1500 cố định (neutral, không nhiễu)
        // ─────────────────────────────────────────────────────────
        uint16_t crsf_channels[16];

        // QUAN TRỌNG: Khởi tạo TOÀN BỘ array = 1500 trước
        // Tránh giá trị rác stack tại ch13-16 gây nhiễu
        for (int i = 0; i < 16; i++) crsf_channels[i] = 1500;

        // Ch1-4: Joy (channels[0-3]) — đã đúng thứ tự từ mixer
        // channels[0..3] da duoc loc boi channel_stabilize() trong sensor_task
        crsf_channels[0] = snap.channels[0];  // Ch1: Throttle
        crsf_channels[1] = snap.channels[1];  // Ch2: Yaw
        crsf_channels[2] = snap.channels[2];  // Ch3: Pitch
        crsf_channels[3] = snap.channels[3];  // Ch4: Roll

        // Ch5-8: 4 công tắc 2 nấc (channels[4-7])
        crsf_channels[4] = snap.channels[4];  // Ch5: ARM
        crsf_channels[5] = snap.channels[5];  // Ch6: Beeper
        crsf_channels[6] = snap.channels[6];  // Ch7: LED
        crsf_channels[7] = snap.channels[7];  // Ch8: AUX

        // Ch9-10: 2 công tắc 3 nấc (channels[8-9])
        crsf_channels[8] = snap.channels[8];  // Ch9: Flight Mode (Stab/Alt/Loiter)
        crsf_channels[9] = snap.channels[9];  // Ch10: Sys Mode (Flight/Menu/Sim)

        // Ch11-12: 2 biến trở (channels[10-11])
        crsf_channels[10] = snap.channels[10]; // Ch11: P1
        crsf_channels[11] = snap.channels[11]; // Ch12: P2

        // Ch13-16: đã được set 1500 ở trên, không cần làm gì thêm

        crsf_send_channels(crsf_channels);


        // B5: Log định kỳ (2 giây/lần)
        static uint32_t last_log = 0;
        uint32_t now = xTaskGetTickCount();
        if (now - last_log > pdMS_TO_TICKS(2000)) {
#if LOG_LATENCY_DEBUG
            uint32_t send_ts = (uint32_t)esp_timer_get_time();
            uint32_t lag = send_ts - snap.trace.ts_sampled;
            ESP_LOGI("LATENCY_DEBUG", "[Radio] Frame %lu | Sensor -> CRSF TX: %lu us (%.2f ms)", 
                     snap.trace.frame_id, lag, lag / 1000.0f);
#endif
#if LOG_CRSF_PERIODIC
            ESP_LOGI(TAG, "[CRSF] Sending 250Hz | Thr: %d | Armed: %d", 
                     snap.channels[0], (snap.channels[5] > 1500));
#endif
            last_log = now;
        }

        // B6: Ngủ bù thời gian - Đảm bảo 250Hz
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(4));
    }
}
