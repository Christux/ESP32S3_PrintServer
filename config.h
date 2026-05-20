#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// ESP32-S3 WiFi Print Server — 設定檔
// 適用印表機：Brother HL-1110
// 開發板：ESP32-S3-N16R8
// ============================================================

// --- WiFi 預設設定 ---
// 首次啟動時會進入 AP 模式讓你設定，這裡是備用預設值
#define DEFAULT_WIFI_SSID     ""
#define DEFAULT_WIFI_PASSWORD ""

// --- AP 模式設定（首次設定用）---
#define AP_SSID       "PrintServer-Setup"
#define AP_PASSWORD   "12345678"

// --- 列印伺服器設定 ---
#define PRINTER_NAME       "Brother_HL1110_WiFi"
#define RAW_SOCKET_PORT    9100    // JetDirect/AppSocket 協定埠
#define WEB_SERVER_PORT    80      // Web 管理介面埠
#define MDNS_HOSTNAME      "hl1110"  // 可用 hl1110.local 存取

// --- USB Host 設定 ---
#define USB_HOST_TASK_PRIORITY    5
#define USB_HOST_TASK_STACK_SIZE  4096
#define PRINT_BUFFER_SIZE         (512 * 1024)  // 512KB 列印緩衝區（存在 PSRAM）

// --- 狀態 LED（可選）---
#define LED_PIN           2       // 板載 LED（依你的板子調整）
#define LED_WIFI_OK       1       // WiFi 已連線
#define LED_PRINTING      2       // 列印中
#define LED_ERROR         3       // 錯誤

// --- OTA 更新 ---
#define OTA_HOSTNAME      "printserver"
#define OTA_PASSWORD      ""      // 留空表示不需密碼

// --- 看門狗 ---
#define WDT_TIMEOUT_SEC   30      // 看門狗超時秒數

// --- NVS 儲存鍵名 ---
#define NVS_NAMESPACE     "printsvr"
#define NVS_KEY_SSID      "wifi_ssid"
#define NVS_KEY_PASS      "wifi_pass"
#define NVS_KEY_HOSTNAME  "hostname"

#endif // CONFIG_H
