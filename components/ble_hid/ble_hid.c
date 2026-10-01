#include "ble_hid.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"
#include "store/config/ble_store_config.h"

void ble_store_config_init(void);

static const char *TAG = "BLE_NIMBLE_HID";

// --- HID REPORT DESCRIPTOR ---
// 6 Axes (Unsigned 16-bit), 16 Buttons
static const uint8_t hid_report_desc[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop Ctrls)
    0x09, 0x05,        // Usage (Game Pad)
    0xA1, 0x01,        // Collection (Application)
    
    0x85, 0x01,        //   Report ID (1)
    
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x09, 0x30,        //     Usage (X)  -> Yaw
    0x09, 0x31,        //     Usage (Y)  -> Throttle
    0x09, 0x32,        //     Usage (Z)  -> Roll
    0x09, 0x33,        //     Usage (Rx) -> Pitch
    0x09, 0x34,        //     Usage (Ry) -> P1
    0x09, 0x35,        //     Usage (Rz) -> P2
    0x15, 0x00,        //     Logical Minimum (0)
    0x27, 0xFF, 0xFF, 0x00, 0x00, // Logical Maximum (65535)
    0x75, 0x10,        //     Report Size (16 bits)
    0x95, 0x06,        //     Report Count (6 axes)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0xC0,              //   End Collection
    
    // 16 Buttons
    0x05, 0x09,        //   Usage Page (Button)
    0x19, 0x01,        //   Usage Minimum (Button 1)
    0x29, 0x10,        //   Usage Maximum (Button 16)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1 bit)
    0x95, 0x10,        //   Report Count (16 buttons)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // D-Pad (Hat Switch) - Required by iOS to map to GCController
    0x05, 0x01,        //   Usage Page (Generic Desktop Ctrls)
    0x09, 0x39,        //   Usage (Hat switch)
    0x15, 0x01,        //   Logical Minimum (1)
    0x25, 0x08,        //   Logical Maximum (8)
    0x35, 0x00,        //   Physical Minimum (0)
    0x46, 0x3B, 0x01,  //   Physical Maximum (315)
    0x65, 0x14,        //   Unit (Eng Rot:Angular Pos)
    0x75, 0x04,        //   Report Size (4 bits)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x42,        //   Input (Data, Variable, Absolute, Null State)
    
    // Padding
    0x75, 0x04,        //   Report Size (4 bits)
    0x95, 0x01,        //   Report Count (1)
    0x81, 0x03,        //   Input (Constant, Variable, Absolute)
    
    0xC0               // End Collection
};

typedef struct {
    uint16_t x;      // Yaw
    uint16_t y;      // Throttle
    uint16_t z;      // Roll
    uint16_t rx;     // Pitch
    uint16_t ry;     // P1
    uint16_t rz;     // P2
    uint8_t  dpad;   // D-Pad (0 = neutral)
    uint16_t buttons;
} __attribute__((packed)) gamepad_report_t;

static volatile bool   s_is_connected = false;
static volatile bool   s_is_subscribed = false;
static uint32_t        s_conn_time = 0;
static uint16_t        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static uint16_t        s_hid_report_handle;
static bool            s_ble_initialized = false;
static uint8_t         s_battery_level = 100;

// GATT Access Callbacks
static int gatt_svr_chr_access_hid(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_batt(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg);
static int gatt_svr_chr_access_devinfo(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg);

static int ble_hid_gap_event(struct ble_gap_event *event, void *arg);

// HID Info (bcdHID, bCountryCode, Flags)
static const uint8_t hid_info[] = {0x11, 0x01, 0x00, 0x01};
static uint8_t hid_control_point = 0;
// Report Reference: Report ID 1, Input Report (1)
static const uint8_t hid_report_ref[] = {0x01, 0x01};

// PnP ID: Vendor ID Source (2=USB), Vendor ID, Product ID, Product Version
static const uint8_t pnp_id[] = {
    0x02,       // Vendor ID Source: 0x02 (USB)
    0x09, 0x12, // Vendor ID: 0x1209 (Open Source VID)
    0x01, 0x10, // Product ID: 0x1001 (Generic Flight Stick/Gamepad)
    0x10, 0x01  // Product Version: 0x0110 (1.1.0)
};

static const struct ble_gatt_svc_def gatt_svr_svcs[] = {
    {
        // Device Information Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180A),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A50), // PnP ID
                .access_cb = gatt_svr_chr_access_devinfo,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A29), // Manufacturer Name
                .access_cb = gatt_svr_chr_access_devinfo,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            { 0 }
        }
    },
    {
        // Battery Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x180F),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A19),
                .access_cb = gatt_svr_chr_access_batt,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
            },
            { 0 }
        }
    },
    {
        // Human Interface Device Service
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(0x1812),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4A), // HID Information
                .access_cb = gatt_svr_chr_access_hid,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4B), // Report Map
                .access_cb = gatt_svr_chr_access_hid,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4C), // HID Control Point
                .access_cb = gatt_svr_chr_access_hid,
                .flags = BLE_GATT_CHR_F_WRITE_NO_RSP,
            },
            {
                .uuid = BLE_UUID16_DECLARE(0x2A4D), // Report
                .access_cb = gatt_svr_chr_access_hid,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_READ_ENC | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_hid_report_handle,
                .descriptors = (struct ble_gatt_dsc_def[]) {
                    {
                        .uuid = BLE_UUID16_DECLARE(0x2908), // Report Reference
                        .access_cb = gatt_svr_chr_access_hid,
                        .att_flags = BLE_ATT_F_READ | BLE_ATT_F_READ_ENC,
                    },
                    { 0 }
                }
            },
            { 0 }
        }
    },
    { 0 }
};

static int gatt_svr_chr_access_devinfo(uint16_t conn_handle, uint16_t attr_handle,
                                       struct ble_gatt_access_ctxt *ctxt, void *arg) {
    uint16_t uuid = ble_uuid_u16(ctxt->chr->uuid);
    if (uuid == 0x2A50) {
        int rc = os_mbuf_append(ctxt->om, pnp_id, sizeof(pnp_id));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (uuid == 0x2A29) {
        const char *manuf = "Zynh Drone";
        int rc = os_mbuf_append(ctxt->om, manuf, strlen(manuf));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } else if (uuid == 0x2A29) {
        const char *manuf = "Zynh Drone";
        int rc = os_mbuf_append(ctxt->om, manuf, strlen(manuf));
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    } 
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_svr_chr_access_batt(uint16_t conn_handle, uint16_t attr_handle,
                                    struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ble_uuid_u16(ctxt->chr->uuid) == 0x2A19) {
        int rc = os_mbuf_append(ctxt->om, &s_battery_level, 1);
        return rc == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static int gatt_svr_chr_access_hid(uint16_t conn_handle, uint16_t attr_handle,
                                   struct ble_gatt_access_ctxt *ctxt, void *arg) {
    uint16_t uuid;
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_DSC) {
        uuid = ble_uuid_u16(ctxt->dsc->uuid);
        if (uuid == 0x2908) {
            os_mbuf_append(ctxt->om, hid_report_ref, sizeof(hid_report_ref));
            return 0;
        }
    } else {
        uuid = ble_uuid_u16(ctxt->chr->uuid);
    }

    if (uuid == 0x2A4A) {
        os_mbuf_append(ctxt->om, hid_info, sizeof(hid_info));
        return 0;
    } else if (uuid == 0x2A4B) {
        os_mbuf_append(ctxt->om, hid_report_desc, sizeof(hid_report_desc));
        return 0;
    } else if (uuid == 0x2A4C) {
        struct os_mbuf *om = ctxt->om;
        if (OS_MBUF_PKTLEN(om) == 1) {
            os_mbuf_copydata(om, 0, 1, &hid_control_point);
        }
        return 0;
    } else if (uuid == 0x2A4D) { // Report
        // Read logic (Host poll)
        gamepad_report_t r = {0};
        os_mbuf_append(ctxt->om, &r, sizeof(r));
        return 0;
    }
    return BLE_ATT_ERR_UNLIKELY;
}

static void ble_hid_advertise(void) {
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    memset(&fields, 0, sizeof fields);
    
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    
    // Appearance 0x03C4 (Gamepad) -> Tương thích tốt hơn với Android/iOS Game Controller Framework
    fields.appearance = 0x03C4;
    fields.appearance_is_present = 1;

    fields.uuids16 = (ble_uuid16_t[]) {
        BLE_UUID16_INIT(0x1812), // HID
        BLE_UUID16_INIT(0x180F)  // Battery
    };
    fields.num_uuids16 = 2;
    fields.uuids16_is_complete = 1;

    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting advertisement data: %d", rc);
        return;
    }

    // Gắn Tên Thiết Bị vào Scan Response để Windows Auto-Reconnect
    memset(&rsp_fields, 0, sizeof rsp_fields);
    rsp_fields.name = (uint8_t *)"Aegis-TX FPV Gamepad";
    rsp_fields.name_len = strlen("Aegis-TX FPV Gamepad");
    rsp_fields.name_is_complete = 1;
    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error setting scan response data: %d", rc);
        return;
    }

    memset(&adv_params, 0, sizeof adv_params);
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN; 
    
    rc = ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                           &adv_params, ble_hid_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error enabling advertising: %d", rc);
    }
}

static int ble_hid_gap_event(struct ble_gap_event *event, void *arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_CONNECT:
            ESP_LOGI(TAG, "BLE CONNECT");
            if (event->connect.status == 0) {
                s_is_connected = true;
                s_is_subscribed = false; // Bắt buộc đợi Host Đăng ký CCCD mới được gửi!
                s_conn_time = xTaskGetTickCount();
                s_conn_handle = event->connect.conn_handle;
            } else {
                ble_hid_advertise();
            }
            break;
            
        case BLE_GAP_EVENT_DISCONNECT:
            ESP_LOGI(TAG, "BLE DISCONNECT, rsn %d", event->disconnect.reason);
            s_is_connected = false;
            s_is_subscribed = false;
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            break;
            
        case BLE_GAP_EVENT_ENC_CHANGE:
            ESP_LOGI(TAG, "Encryption change: status=%d", event->enc_change.status);
            break;
            
        case BLE_GAP_EVENT_SUBSCRIBE:
            ESP_LOGI(TAG, "Subscribe event: attr_handle=%d, notify=%d", 
                     event->subscribe.attr_handle, event->subscribe.cur_notify);
            if (event->subscribe.attr_handle == s_hid_report_handle) {
                s_is_subscribed = event->subscribe.cur_notify;
                if (s_is_subscribed) {
                    ESP_LOGI(TAG, ">>> WINDOWS SUBSCRIBED: HID DATA STREAM STARTING! <<<");
                } else {
                    ESP_LOGI(TAG, ">>> WINDOWS UNSUBSCRIBED: DATA STREAM PAUSED. <<<");
                }
            }
            break;

        case BLE_GAP_EVENT_REPEAT_PAIRING: {
            // Xử lý khi Windows xóa thiết bị và kết nối lại
            // Xóa bond cũ trên mạch để tự động khớp bond mới
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->repeat_pairing.conn_handle, &desc) == 0) {
                ble_store_util_delete_peer(&desc.peer_id_addr);
            }
            return BLE_GAP_REPEAT_PAIRING_RETRY;
        }
    }
    return 0;
}

static void ble_hid_on_reset(int reason) {
    ESP_LOGE(TAG, "Resetting state; reason=%d", reason);
}

static void ble_hid_on_sync(void) {
    int rc;

    rc = ble_hs_util_ensure_addr(0);
    assert(rc == 0);

    // Just Works Pairing (Mặc định ổn định nhất cho Windows)
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_mitm = 0; // Tắt MITM để không bị đòi xác nhận trên Windows
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_io_cap = BLE_SM_IO_CAP_NO_IO; 
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC | BLE_SM_PAIR_KEY_DIST_ID;

    // Set Default Tên Thiết Bị
    ble_svc_gap_device_name_set("Aegis-TX FPV Gamepad");
    ble_svc_gap_device_appearance_set(0x03C4); // Gamepad

    ble_hid_advertise();
}

void nimble_host_task(void *param) {
    ESP_LOGI(TAG, "NimBLE Host Task Started");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_hid_init(void) {
    if (s_ble_initialized) {
        ble_hid_advertise();
        ESP_LOGI(TAG, "BLE HID Resumed Advertising");
        return;
    }
    
    // Tắt các Log rác (Spam) của NimBLE (Nguyên nhân gây freeze Core do nghẽn UART)
    esp_log_level_set("NimBLE", ESP_LOG_WARN);

    // NimBLE INIT
    nimble_port_init();
    
    ble_hs_cfg.reset_cb = ble_hid_on_reset;
    ble_hs_cfg.sync_cb = ble_hid_on_sync;
    ble_hs_cfg.gatts_register_cb = NULL;
    
    // Đăng ký dịch vụ GATTS
    int rc = ble_gatts_count_cfg(gatt_svr_svcs);
    assert(rc == 0);
    rc = ble_gatts_add_svcs(gatt_svr_svcs);
    assert(rc == 0);

    // Thiết lập GAP & GATT Service tiêu chuẩn
    ble_svc_gap_init(); 
    ble_svc_gatt_init();

    // Khởi tạo Keystore NVS để lưu Bond (Bắt buộc cho Windows 11)
    ble_store_config_init();

    // Đẩy GAP Event Handle
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    
    // PIN NIMBLE TO CORE 0: Tránh làm treo UI Task trên Core 1
    xTaskCreatePinnedToCore(nimble_host_task, "ble_host", 4096, NULL, 17, NULL, 0);
    
    s_ble_initialized = true;
    ESP_LOGI(TAG, "BLE HID Initialized & NimBLE Core Booted");
}

void ble_hid_deinit(void) {
    if (!s_ble_initialized) return;

    // Thay vì hủy NimBLE Core (Rủi ro rò rỉ bộ nhớ), chúng ta chỉ Cúp sóng (Stop Adv)
    if (s_is_connected) {
        ble_gap_terminate(s_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
    ble_gap_adv_stop();
    s_is_connected = false;
    
    ESP_LOGI(TAG, "BLE HID Advertising Suspended (Stack Kept Alive)");
}

bool ble_hid_is_connected(void) {
    return s_is_connected;
}

// map_axis: Calib + Invert + Scale 1000-2000 -> 0-65535
// channels[] da duoc channel_stabilize() loc truoc roi - khong can deadband o day nua.
static uint16_t map_axis(uint16_t val, bool invert, uint8_t axis_idx) {
    (void)axis_idx; // Khong dung nua, giu de khong anh huong caller
    if (val < 1000) val = 1000;
    if (val > 2000) val = 2000;
    if (invert) val = 3000 - val;
    return (uint16_t)(((uint32_t)(val - 1000) * 65535) / 1000);
}

void ble_hid_send_report(uint16_t throttle, uint16_t yaw, uint16_t pitch, uint16_t roll, uint16_t p1, uint16_t p2, uint16_t buttons) {
    if (!s_is_connected || !s_is_subscribed) return;

    uint32_t now = xTaskGetTickCount();
    if ((now - s_conn_time) < pdMS_TO_TICKS(1500)) return;

    // Steam Gamepad mapping chuẩn:
    // Left Stick: X (Yaw), Y (Throttle)
    // Right Stick: Z (Roll), Rz (Pitch)
    // Triggers / Slider: Rx (P1), Ry (P2)
    uint16_t v_yaw   = map_axis(yaw,      false, 0);  // X
    uint16_t v_thr   = map_axis(throttle, true,  1);  // Y
    uint16_t v_roll  = map_axis(roll,     false, 2);  // Z
    uint16_t v_pitch = map_axis(pitch,    true,  3);  // Rx
    uint16_t v_p1    = map_axis(p1,       false, 4);  // Ry
    uint16_t v_p2    = map_axis(p2,       false, 5);  // Rz

    static uint32_t last_send_time = 0;
    if ((now - last_send_time) < pdMS_TO_TICKS(10)) return; // Optimized 100Hz Signal!
    last_send_time = now;

    gamepad_report_t report;
    report.x  = v_yaw;
    report.y  = v_thr;
    report.z  = v_roll;
    report.rx = v_pitch;
    report.ry = v_p1;
    report.rz = v_p2;
    report.dpad = 0; // Neutral state (không bấm)
    report.buttons = buttons;

    struct os_mbuf *om = ble_hs_mbuf_from_flat(&report, sizeof(report));
    if (om) {
        int rc = ble_gatts_notify_custom(s_conn_handle, s_hid_report_handle, om);
        if (rc == 0) {
            // Success
        }
    }
}
