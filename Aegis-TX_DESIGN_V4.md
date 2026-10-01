# 🦅 Aegis-TX TRANSMITTER OS - PHÁC THẢO THIẾT KẾ v4.0 (ELITE)

Bản thiết kế này hợp nhất các ý tưởng từ đội ngũ kiến trúc sư và chuyên gia nhúng để xây dựng một nền tảng tay cầm điều khiển chuyên nghiệp chuẩn thương mại trên ESP32.

---

## 1. TẦNG VẬT LÝ & ÁNH XẠ KÊNH (HARDWARE MAP)

### 1.1. Bố trí công tắc (Switch Layout)
| Linh kiện | Loại | Vị trí | Chức năng (Lên / Giữa / Xuống) |
| :--- | :--- | :--- | :--- |
| **SW_MAIN** | 3-Pos | Dưới - Phải | **FLIGHT** (Bay) / **MENU** (Cài đặt) / **SIM** (Giả lập) |
| **SW_FLIGHT**| 3-Pos | Trên - Phải | **Mode 1** / **Mode 2** / **Mode 3** (Gửi PX4) |
| **4x SW_AUX** | 2-Pos | Trên cùng | **ARM** / **POSHOLD** / **AUX1** / **AUX2** |

### 1.2. Ánh xạ 12 Kênh (Channel Mapping to PX4)
*   **CH1 - CH4**: Aileron, Elevator, Throttle, Rudder (Joystick).
*   **CH5**: Flight Mode (Công tắc 3 nấc Phải).
*   **CH6**: ARM Command.
*   **CH7**: Position Hold.
*   **CH8 - CH12**: Mở rộng (Cần gạt, Potentiometer).

---

## 2. CỖ MÁY TRẠNG THÁI (CORE FSM)

Toàn bộ hệ thống vận hành theo 3 trạng thái độc lập dựa trên **SW_MAIN**:

### ✈️ nấc 1: FLIGHT MODE
*   **Radio (Core 0)**: Hoạt động ưu tiên cao nhất (250Hz), độ trễ cực thấp.
*   **UI (Core 1)**: Hiển thị Dashboard (Pin, Sóng, Telemetry, Hẹn giờ bay). Khóa Menu an toàn.
*   **Input**: Gậy điều khiển máy bay.

### 🛠️ nấc 2: MENU MODE
*   **Radio (Core 0)**: Ngắt phát sóng hoặc khóa gậy an toàn (Avoid flyaway).
*   **UI (Core 1)**: Hiển thị hệ thống Menu List-view.
*   **Input**: Joystick trái/phải dùng làm phím điều hướng (Up/Down/Select).

### 🎮 nấc 3: SIMULATOR MODE
*   **Radio (Core 0)**: Tắt RF để tiết kiệm pin. Kích hoạt Bluetooth BLE HID.
*   **UI (Core 1)**: Hiển thị trạng thái kết nối "Sim Mode Active".
*   **Input**: Chuyển đổi dữ liệu RC sang chuẩn Gamepad PC.

---

## 3. CƠ CẤU MENU CÀI ĐẶT (PRO FEATURE LIST)

Thiết kế theo chuẩn EdgeTX với 6 nhóm chức năng chính:

### 📂 A. Model Setup (Cấu hình Máy bay)
1.  **Model Name**: Đổi tên cho từng máy bay riêng biệt.
2.  **Timers**: Quản lý giờ bay (Flight Time) và tổng giờ tay cầm.
3.  **Protocol**: Chọn giao thức bắn sóng (Bayang/ELRS/Sim).

### 📂 B. Mixer & Logic (Linh hồn tay cầm)
4.  **Expo & Rates**: Chỉnh độ gắt/êm của gậy (Exponential Curve).
5.  **Reverse**: Đảo chiều quay cho Servo/ESC.
6.  **Sub-trims & Endpoints**: Cân bằng điểm chết và giới hạn hành trình gậy (1000-2000uS).

### 📂 C. Hardware & System (Hệ thống)
7.  **Stick Calibration**: (Core) Thuật toán hướng dẫn xoay gậy để lấy Min/Max/Center.
8.  **Touch Calibration**: Cân chỉnh lại tọa độ màn hình cảm ứng.
9.  **Battery & Audio**: Cài đặt báo động pin yếu và âm lượng còi chip.

---

## 4. CHIẾN THUẬT TRIỂN KHAI (ROADMAP)

### 🔴 Phase 1: Lõi điều phối (FSM Engine)
*   Xây dựng bộ quản lý chuyển đổi Flight/Menu/Sim dựa trên công tắc vật lý.
*   Tách biệt tuyệt đối tài nguyên Radio và tài nguyên UI.

### 🟡 Phase 2: Input Engine & Calibration
*   Triển khai thuật toán **Calibration** để chuẩn hóa dữ liệu đầu vào.
*   Lưu trữ thông số Calib vào NVS Flash.

### 🟢 Phase 3: UI Menu Framework
*   Dùng LVGL xây dựng khung Menu có khả năng cuộn.
*   Tích hợp các Widget Slider và Toggle cho các trang cài đặt.

### 🔵 Phase 4: Tính năng nâng cao (Commercial Grade)
*   Telemetry Dashboard (Pin máy bay, RSSI, GPS).
*   Simulator Mode qua BLE HID.
*   Hệ thống cảnh báo bằng âm thanh và rung.

---
**Aegis-TX v4.0 - Engineered for Excellence.**
