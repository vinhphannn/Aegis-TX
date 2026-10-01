# 🛠️ Nhật Ký Debug ESP32 BLE HID Gamepad (Aegis-TX)

Tài liệu này lưu lại những phát hiện quan trọng nhất trong quá trình phát triển tính năng giả lập (BLE Simulator) cho tay cầm Aegis-TX. Đây là những "bí mật sâu kín" của hệ điều hành Windows 11 và ESP-IDF (NimBLE) mà chúng ta đã phải đổ rất nhiều mồ hôi để tìm ra.

---

## 🛑 1. Lỗi: Kết nối rồi ngắt ngay lập tức sau ~10 giây (rsn 531)
**Hiện tượng:** 
Tay cầm kết nối với Windows, nhưng bị kẹt ở chữ "Connecting...". Khoảng 10-13 giây sau, kết nối bị đá văng với mã lỗi ngắt kết nối `rsn 531` (Remote User Terminated Connection). Mọi nỗ lực tinh chỉnh HID Descriptor đều vô ích.

**Nguyên nhân gốc rễ (Root Cause):**
Khi sử dụng **Secure Connections (ECDH)** để thiết lập Pairing (Bonding), ESP32 cần một lượng lớn CPU để giải bài toán mã hóa đường cong elliptic.
Tuy nhiên, trong kiến trúc của Aegis-TX, `nimble_host_task` và `sensor_task` được cấp phát **cùng một mức độ ưu tiên (Priority = 15)** trên Core 0.
Vì `sensor_task` liên tục đọc ADC và xử lý I2C/SPI với tần số rất cao, nó đã chiếm đoạt (starve) gần hết CPU của NimBLE. Do đó, thuật toán mã hóa chạy quá chậm và vượt quá mốc thời gian giới hạn 10 giây (SMP Timeout) của Windows. Quá hạn, Windows bực mình và ngắt kết nối.

**Cách khắc phục:**
- Nâng độ ưu tiên của tác vụ `nimble_host_task` lên `17` (cao hơn `sensor_task` ở mức 15). Giúp luồng dữ liệu Bluetooth luôn mượt mà.
- Đảm bảo duy trì Legacy Just Works Pairing (ổn định nhất cho Gamepad).

---

## 🛑 2. Lỗi: Cần gạt "câm điếc", Windows nhận nhưng Test không chạy
**Hiện tượng:**
Tay cầm đã Pair thành công, không còn bị ngắt kết nối. Nhưng vào Steam hoặc trang Gamepad Tester thì bẻ Joystick hay bấm nút đều không có bất kỳ phản hồi nào.

**Nguyên nhân gốc rễ (Root Cause):**
Chúng ta đã cấu hình **PnP ID** giả danh tay cầm **Xbox 360** (Vendor ID: `0x045E`, Product ID: `0x028E`) để mong game nhận diện nhanh hơn.
**Tuy nhiên**, Windows 11 có một cơ chế cực kỳ độc đoán: Hễ thấy thiết bị mang PnP ID của Xbox, nó sẽ ép tải driver `XInput` (`xusb22.sys`). 
Driver XInput mong đợi nhận được các gói tin (Report) dài **20-byte theo định dạng mã hóa độc quyền** của Microsoft. Do gói tin HID Report của chúng ta được thiết kế theo chuẩn DirectInput (DInput) độ dài 14-byte, driver của Microsoft đã thẳng tay "vứt bỏ" toàn bộ tín hiệu.

**Cách khắc phục:**
- Chuyển PnP ID sang mã nguồn mở Generic Gamepad (Vendor ID: `0x1209`, Product ID: `0x1001`).
- Lúc này, Windows sẽ chịu buông tha cho chúng ta và sử dụng driver phổ thông `Generic DirectInput`. Driver này sẽ tuân thủ tuyệt đối cấu trúc *HID Report Descriptor* 6 trục, 6 nút mà chúng ta đã tự viết.

---

## 🛑 3. Lỗi: Ngắt kết nối ngay lập tức (< 0.1s) sau khi khởi động lại
**Hiện tượng:**
Kết nối thành công. Nhưng nếu tắt tay cầm đi rồi bật lại, hoặc nạp lại code, hễ kết nối vào máy tính là bị đá văng tức khắc (nhanh đến mức chưa tới 100ms).

**Nguyên nhân gốc rễ (Root Cause):**
Đây là hiện tượng **Bonding Mismatch Deadlock** kinh điển của Bluetooth LE.
- Để thử nghiệm, chúng ta đã thêm lệnh `ble_store_clear();` vào hàm khởi tạo để "xóa sạch trí nhớ" của ESP32 mỗi khi khởi động.
- Windows thì vẫn nhớ tay cầm và giữ lại chìa khóa (Link Key) đã mã hóa từ lần kết nối trước.
- Khi gặp nhau lại, Windows đòi dùng khóa cũ, nhưng ESP32 bảo "Tao không nhớ mày là ai". Phát hiện sự bất đồng mã hóa, Windows nghi ngờ đây là cuộc tấn công đánh cắp dữ liệu (MITM) nên ngay lập tức cắt đứt kết nối để bảo mật.

**Cách khắc phục:**
- Tuyệt đối không dùng `ble_store_clear()` trong production. ESP32 phải luôn được phép giữ lại chìa khóa mã hóa vào bộ nhớ flash (NVS) thông qua `ble_store_config_init()`.
- Mỗi khi nâng cấp tính năng làm thay đổi cấu trúc mã hóa, người dùng phải chủ động bấm "Remove Device" trong Settings của Windows để xóa khóa cũ.

---

## 🛑 4. Lỗi: Trục Y và Ry bị ngược trên Steam / Liftoff
**Hiện tượng:**
Chơi giả lập, đẩy cần ga (Throttle) lên thì Drone lại hạ xuống. Kéo cần Pitch xuống thì Drone lại chúi mũi tới trước.

**Nguyên nhân gốc rễ (Root Cause):**
Mạch Radio chuẩn sử dụng mức PWM từ 1000us đến 2000us. Tuy nhiên, chuẩn DirectX / USB HID quy ước giá trị trục Y (lên/xuống) là: **Lên = Min (0), Xuống = Max (65535)**.
Nếu ta chuyển đổi tịnh tiến (Map) thẳng từ PWM sang HID thì kết quả sẽ bị lộn ngược hoàn toàn theo góc nhìn của Windows.

**Cách khắc phục:**
- Xây dựng thuật toán `map_axis()` riêng biệt và thêm tham số `invert`.
- Trục X (Yaw, Roll) giữ nguyên.
- Trục Y (Throttle, Pitch) được truyền cờ `invert = true` để ESP32 chủ động lật ngược con số `(65535 - mapped)` ngay từ dưới phần cứng. Kết quả là mọi phần mềm trên Windows đều nhận diện đúng mà không cần mất công calibrate.

---
*Tài liệu được đúc kết từ hàng chục lần test thực tế trên Aegis-TX.*


## ?? 5. L?i: Payload Length Mismatch (S? kh�c bi?t t? huy?t gi?a USB HID v� BLE HID)
**Hi?n tu?ng:**
Windows nh?n thi?t b? l� Generic Gamepad, v�o Steam nh?n d?ng d�ng t�n thi?t b?, kh�ng r?t k?t n?i. Nhung t?t c? c�c tr?c v� n�t b?m d?u c�m di?c, Calibration kh�ng c� b?t k� ph?n h?i n�o.

**Nguy�n nh�n g?c r? (Root Cause):**
Trong chu?n USB HID th�ng thu?ng, n?u b?n khai b�o Report ID trong Descriptor, b?n B?T BU?C ph?i ch�n m� Report ID (1 byte) v�o d?u m?i g�i tin d? li?u g?i di. V� d?: D? li?u 13 byte + 1 byte ID = 14 byte.
**Tuy nhi�n, v?i chu?n BLE HID (HOGP - HID over GATT Profile):** Bang th�ng Bluetooth r?t qu� gi�, v� m?i Report d� du?c ph�n l?p th�nh c�c GATT Characteristic ri�ng bi?t. Do d�, chu?n HOGP quy d?nh: **TUY?T �?I KH�NG �U?C G?I K�M REPORT ID** trong g�i tin Payload.
Ch�ng ta d� gi? l?i bi?n uint8_t report_id trong struct gamepad_report_t, khi?n cho ESP32 g?i di 14 byte. Windows ph�n t�ch HID Descriptor v� t�nh to�n ra r?ng g�i d? li?u ch? c� 13 byte (6 tr?c x 2 byte + 1 byte n�t b?m). Th?y 14 != 13, driver HID c?a Windows d� th?ng tay v?t b? to�n b? g�i tin v� cho r?ng d? li?u b? l?i/corrupt!

**C�ch kh?c ph?c:**
X�a b? bi?n eport_id ra kh?i struct d? li?u gamepad_report_t v� ng?ng g�n n� trong h�m le_hid_send_report. C?u tr�c g�i tin gi?m v? d�ng 13 byte. Ngay l?p t?c Windows ch?p nh?n v� truy?n th?ng d? li?u v�o Steam/Liftoff.

**��NH CH�NH L?I (S? th?t v? Report ID trong BLE HID):**
Sau khi quan s�t ki d? li?u b? 'nh?y lung tung' v� 'l?ch tr?c', m?t s? th?t m?i du?c phoi b�y: N?u trong b?ng khai b�o HID (HID Descriptor) c� nh?c d?n \Report ID\, th� g�i tin BLE (GATT Notification) **B?T BU?C** ph?i c� byte \Report ID\ ? v? tr� d?u ti�n. Khi ch�ng ta th�o \eport_id\ ra kh?i c?u tr�c d? li?u, Windows v?n m?c d?nh byte d?u ti�n n� nh?n du?c l� Report ID. K?t qu? l� n� l?y byte th?p c?a tr?c X l�m Report ID, v� x� d?ch (shift) to�n b? c�c byte c�n l?i di 1 v? tr�. �i?u n�y khi?n byte cao c?a tr?c X gh�p v?i byte th?p c?a tr?c Y, t?o ra c�c con s? v� nghia, nh?y lo?n x?! Vi?c th�m l?i bi?n \eport_id\ v�o Payload d� gi?i quy?t tri?t d? l?i l?ch d? li?u n�y.

## ?? 6. L?i: Tr?c Z (Roll) b? 't�ng h�nh' do b?c trong Pointer Collection
**Hi?n tu?ng:**
Sau khi gi?i quy?t xong l?i c?u tr�c v� byte shift, m?i tr?c d?u nh?n chu?n x�c, DUY NH?T tr?c Z (tuong duong Roll) bi?n m?t kh�ng d? l?i d?u v?t. C�c tr?c Rx, Ry, Rz, X, Y v?n nh?y b�nh thu?ng.

**Nguy�n nh�n g?c r? (Root Cause):**
Trong HID Descriptor, m�nh d� nh�m 6 tr?c (X, Y, Z, Rx, Ry, Rz) v�o trong m?t t?p h?p v?t l� mang t�n \Collection (Pointer)\. 
Theo tu duy c?a Windows DirectInput: M?t c�i \Pointer\ (Con tr? chu?t) th� thu?ng ch? c� tr?c X v� Y. N?u n� th?y tr?c \Z\ n?m trong Pointer, n� l?p t?c g�n m�c tr?c Z l� \Mouse Scroll Wheel\ (Con lan chu?t). Tuy nhi�n, v� ch�ng ta khai b�o \Z\ l� gi� tr? tuy?t d?i (\Absolute\), trong khi con lan chu?t l?i c?n gi� tr? tuong d?i (\Relative\), Windows d� x?y ra xung d?t logic v� quy?t d?nh... v?t lu�n tr?c Z v�o s?t r�c!

**C�ch kh?c ph?c:**
X�a b? ho�n to�n l?p b?c \Collection (Pointer)\. Th? t? do cho c? 6 tr?c n?m tr?c ti?p du?i quy?n qu?n l� c?a \Collection (Joystick)\. Khi kh�ng c�n b? g�n m�c l� 'con tr?', Windows l?p t?c nh?n di?n \Z\ l� m?t tr?c Analog d?c l?p c?a Joystick v� hi?n th? n� tr? l?i.


**Fixing iOS Support (FPV.SkyDive Disconnected Issue):**
�i?n tho?i (d?c bi?t l� iOS) s? d?ng b? GCController framework r?t kh?t khe. N� s? B? QUA tay c?m n?u:
1. Thi?t b? khai b�o l� Joystick (0x04) thay v� Game Pad (0x05).
2. B?ng c?u tr�c HID thi?u v?ng n�t D-Pad (Hat Switch). Apple b?t bu?c m?t Gamepad chu?n ph?i c� n�t di?u hu?ng D-Pad, n?u kh�ng n� s? t? ch?i t?o d?i tu?ng GCExtendedGamepad v� c�c game nhu FPV.SkyDive s? kh�ng th? nh�n th?y tay c?m.
C�ch gi?i quy?t: M�nh d� d?i l?i th�nh Game Pad (0x05), Appearance 0x03C4, v� g?n th�m m?t b? m� ph?ng D-Pad ?o (Hat Switch, 1 byte) lu�n ? tr?ng th�i Neutral v�o g�i d? li?u d? d�nh l?a h? di?u h�nh iOS.
