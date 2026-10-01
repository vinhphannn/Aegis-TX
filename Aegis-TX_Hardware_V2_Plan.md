# 🗺️ DỰ ÁN Aegis-TX - KẾ HOẠCH NÂNG CẤP PHẦN CỨNG V2.0

Tài liệu này mô tả chi tiết cách thiết kế lại sơ đồ đi dây cho tay điều khiển Aegis-TX sau khi bổ sung thêm 2 cụm Gimbal (từ CT6B), 2 biến trở núm xoay, và module gộp kênh **CD74HC4067**.

---

## 🚀 TỔNG QUAN GIẢI PHÁP
- **Multiplexer (CD74HC4067)**: Gom 12 tín hiệu (4 trục gimbal + 2 biến trở + 2 CT 3-nấc + 4 CT 2-nấc) vào **duy nhất 1 chân ADC** của ESP32.
- **TFT Touch**: Tích hợp cảm ứng bằng cách sử dụng chung bus SPI của màn hình.
- **Giải phóng chân GPIO**: Giải phóng được ít nhất 10 chân GPIO so với thiết kế cũ, giúp ESP32 chạy nhẹ nhàng và dễ mở rộng.

---

## 📍 SƠ ĐỒ NỐI CHÂN ESP32 (V2.0)

| Linh kiện | Chân chức năng | GPIO | Ghi chú |
| :--- | :--- | :--- | :--- |
| **TFT Display** | SCK / MOSI / MISO | 18 / 23 / 19 | Bus VSPI (MISO chia sẻ với Touch) |
| | CS / DC / RST | 5 / 27 / 4 | Chân 5 (TFT_CS) có pull-up nội |
| | LED (Backlight) | **3.3V*** | **Cắm qua trở 10-22Ω + Tụ lọc** |
| **Màn cảm ứng**| T_CS | **32** | Dùng chung SCK/MOSI/MISO với TFT |
| | T_IRQ | None | Bỏ qua (Tránh rủi ro/tiết kiệm chân) |
| **CRSF** | TX / RX | 16 / 17 | (Tuỳ chọn UART) |
| **MUX 4067** | SIG (Analog Out) | 34 | Chân ADC (Sạch nhiễu, Input Only) |
| | S0 / S1 / S2 / **S3** | 17 / 22 / 33 / **26**| **Né hoàn toàn GPIO15 (Strap pin)** |

---

## 🔢 PHÂN KÊNH MODULE CD74HC4067 (C0 - C15)

Chúng ta sử dụng 12/16 kênh của module Mux:

| Kênh Mux | Chức năng điều khiển | Loại tín hiệu | Ghi chú |
| :--- | :--- | :--- | :--- |
| **C0** | Gimbal Trái - Trục Y (Throttle) | Analog | 1000 - 2000 |
| **C1** | Gimbal Trái - Trục X (Yaw) | Analog | 1000 - 2000 |
| **C2** | Gimbal Phải - Trục Y (Pitch) | Analog | 1000 - 2000 |
| **C3** | Gimbal Phải - Trục X (Roll) | Analog | 1000 - 2000 |
| **C4** | Biến trở núm xoay 1 (P1) | Analog | Kênh AUX 1 |
| **C5** | Biến trở núm xoay 2 (P2) | Analog | Kênh AUX 2 |
| **C6** | Công tắc 3-nấc 1 (Flight Mode) | Analog | Cầu phân áp 2.2k/1k |
| **C7** | Công tắc 3-nấc 2 (Speed) | Analog | Cầu phân áp 2.2k/1k |
| **C8** | Công tắc 2-nấc A (ARM) | Digital* | Đọc qua ADC (Threshold > 2000) |
| **C9** | Công tắc 2-nấc B (Beeper) | Digital* | Đọc qua ADC (Threshold > 2000) |
| **C10** | Công tắc 2-nấc C (LED) | Digital* | Đọc qua ADC (Threshold > 2000) |
| **C11** | Công tắc 2-nấc D (Dự phòng) | Digital* | Đọc qua ADC (Threshold > 2000) |

---

## 🔌 NGUYÊN TẮC KỸ THUẬT V2.0 (BẮT BUỘC)

### 1. Xử lý ADC và Mux (Tốc độ & Ổn định)
- **Chu kỳ đọc**: Phân bổ kênh để giữ loop 250Hz.
    - Loop 1 & 2: Đọc 4 trục Gimbals (Ưu tiên cao nhất).
    - Loop 3: Đọc 2 Biến trở + 2 CT 3-nấc.
    - Loop 4: Đọc 4 CT 2-nấc.
- **Delay chuyển kênh**: Thử nghiệm ở **5µs**, nếu nhiễu tăng lên **10µs** để ổn định tuyệt đối.
- **ADC Mode**: `analogSetAttenuation(ADC_11db)` và `analogReadResolution(12)`.
- **Ổn định mẫu**: Đọc bỏ mẫu đầu, lấy mẫu thứ 2.

### 2. Digital qua ADC (Hysteresis Chuẩn)
- **Ngưỡng kích hoạt**:
    - **HIGH**: > 3000
    - **LOW**: < 1000
- Sử dụng cơ chế Hysteresis để loại bỏ hiện tượng nhảy trạng thái ở vùng biên.

### 3. Failsafe & Counter (An Toàn Bay)
- **TX Counter**: Mỗi gói tin gửi đi kèm một biến `uint8_t counter` tăng dần.
- **RX Check**: Nếu máy bay không thấy counter thay đổi trong > 100ms, tự động đưa ga về 1000.

### 4. Giảm Jitter (Ổn định 250Hz)
- **Tắt nhiễu nền**: Gọi `WiFi.mode(WIFI_OFF)` và `btStop()` để tránh ngắt hệ thống.
- **Task Affinity**: Khóa task điều khiển vào Core 0: `vTaskCoreAffinitySet(NULL, (1 << 0))`.
- **Loop Timing**: Sử dụng `micros()` để kiểm soát chu kỳ chính xác 4000µs.

### 5. Nối đất & Nguồn (Star Ground)
- **Star Ground**: Đi riêng đường GND cho RF và GND sạch cho cụm Analog về điểm chung tại nguồn.
- **Backlight Filter**: LED màn hình cắm qua **1-2 Diode** nối tiếp + Tụ **10µF+100µF** để tránh sụt áp tức thời.

---
**Người lập kế hoạch:** Antigravity AI & USER
**Trạng thái:** V2.0 FINAL - BẤT BẠI (250Hz)
