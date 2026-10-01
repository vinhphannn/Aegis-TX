#include "core/sys_state.h"
#include "core/hal_input.h"   // HAL v5.0 - thay thế rc_input
#include "esp_log.h"
#include "nvs_flash.h"
#include "ui_display.h"
#include <string.h>

static const char *TAG = "AEGIS_TX_MAIN";

// Global Definitions
QueueHandle_t sensor_queue = NULL;
SemaphoreHandle_t sys_mutex   = NULL;
SystemState_t g_state = {0};

// Task Prototypes
extern void sensor_task(void *pvParameters);
extern void radio_task(void *pvParameters);
extern void ui_task(void *pvParameters);

// Storage Prototypes
extern esp_err_t storage_init(void);
extern esp_err_t storage_load_calib(CalibData_t *data);
extern esp_err_t storage_load_model(uint8_t id, ModelConfig_t *model);

void app_main(void) {
    ESP_LOGI(TAG, ">>> Booting Aegis-TX Ground Controller v1.6 (V3.2 Architecture) <<<");

    // 1. Storage & Mutex
    storage_init();
    sys_mutex = xSemaphoreCreateMutex();

    // 2. Load Persisted Configs
    if (storage_load_calib(&g_state.calib) != ESP_OK) {
        ESP_LOGW(TAG, "No calibration found. Using raw defaults.");
        g_state.calib.valid = false;
    } else {
        g_state.calib.valid = true;
        ESP_LOGI(TAG, "Calibration data loaded successfully.");
    }

    if (storage_load_model(0, &g_state.active_model) != ESP_OK) {
        ESP_LOGI(TAG, "Creating Default Model Profile...");
        g_state.active_model.id = 0;
        strcpy(g_state.active_model.name, "Default V3.2");
        // Initialize safe defaults
        for(int i=0; i<4; i++) {
            g_state.active_model.expo[i] = 20; // 20% expo for smooth flight
            g_state.active_model.trims[i] = 0;
        }
    }

    // 3. Hardware HAL Init (v5.0: dùng HAL_Input thay vì RC_Input)
    HAL_Input_Init();
    Display_Init();

    // 4. In-Process Communication
    // NOTE v5.0: sensor_queue không còn là kênh điều phối chính.
    // radio_task đọc g_state trực tiếp qua sys_mutex (zero-copy, zero-delay).
    // Queue giữ lại để ui_task dùng cho các event đặc biệt (Mode change, Alert).
    sensor_queue = xQueueCreate(1, sizeof(SystemState_t));

    ESP_LOGI(TAG, ">>> Spawning V4.0 Dual-Core Architecture <<<");

    /* ===================================================================
     * Ánh xạ CPU Tối ưu cho ESP32 2 nhân:
     *
     * CPU0 (PRO_CPU):
     *   ├ sensor_task  PRIO=15  250Hz  - đọc ADC/MUX, tính mixer
     *   └ radio_task   PRIO=12  250Hz  - TX CRSF, chạy sau sensor (lower prio)
     *   IDLE0 là luôn được chạy khi cả hai task đang sleep/wait
     *
     * CPU1 (Độc quyền UI):
     *   └ ui_task     PRIO= 9  30Hz   - LVGL render, không share với radio
     *   IDLE1 là luôn được chạy trong ~97% thời gian (30Hz × ~5ms = 15%)
     *
     * Tại sao CPU0 mà không CPU1 cho radio?
     *   - radio_task gọi vTaskDelay() bên trong transmit loop → yield CPU0
     *   - sensor_task (PRIO cao hơn) sẽ preempt radio khi cần đọc ADC
     *   - IDLE0 chạy khi cả hai sleep → TWDT0 được feed
     *   - IDLE1 chạy tự do vì ui_task chỉ chiếm ~15% CPU1 → TWDT1 được feed
     * ================================================================= */

    // CPU0: Sensor (PRIO 15) + Radio (PRIO 12)
    xTaskCreatePinnedToCore(sensor_task, "sensor_task", 8192,  NULL, PRIO_SENSOR_TASK, NULL, 0);
    xTaskCreatePinnedToCore(radio_task,  "radio_task",  8192,  NULL, PRIO_RADIO_TASK,  NULL, 0);

    // CPU1: UI độc quyền (thả IDLE1 tự do)
    xTaskCreatePinnedToCore(ui_task,     "ui_task",    24576,  NULL, PRIO_UI_TASK,     NULL, 1);

    ESP_LOGI(TAG, "=== BOOT SEQUENCE COMPLETE ===");
}
