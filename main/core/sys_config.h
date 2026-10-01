#pragma once

/**
 * sys_config.h - Aegis-TX Global Configuration
 * ==========================================================
 * Quản lý các cấu hình biên dịch và cờ bật/tắt tính năng.
 * Thay đổi số 1 thành 0 để tắt các log không cần thiết,
 * giúp màn hình Terminal sạch sẽ và gọn gàng hơn.
 */

// ══════════════════════════════════════════════════════════
// DEBUG & LOGGING SWITCHES
// ══════════════════════════════════════════════════════════

// 1. Log theo dõi độ trễ hệ thống (Latency từ Joy -> RF / UI)
#define LOG_LATENCY_DEBUG           1

// 2. Log bảng báo cáo thời gian thực thi (TIMING REPORT)
#define LOG_TIMING_REPORT           0

// 3. Log chẩn đoán phần cứng MUX (In raw 14 kênh ADC)
#define LOG_MUX_SCAN_RAW            0

// 4. Log báo cáo sức khỏe giao diện UI (Miss rate)
#define LOG_UI_DIAGNOSTICS          0

// 5. Log định kỳ 2s của Radio Task (CRSF TX Status)
#define LOG_CRSF_PERIODIC           0

// 6. Log sự kiện chuyển đổi hệ thống (Flight/Menu/Sim)
#define LOG_SYS_MODE_CHANGES        1
