#ifndef CONFIG_H
#define CONFIG_H

// ============================================================
// ESP32-S3 WiFi Print Server — Configuration
// Compatible printer: Epson EPL-5900
// Board: ESP32-S3-WROOM-1-N8R8 (8MB Flash / 8MB PSRAM)
// ============================================================

// --- WiFi Default Settings ---
// Enters AP mode on first boot for setup; these are fallback defaults
#define DEFAULT_WIFI_SSID     ""
#define DEFAULT_WIFI_PASSWORD ""

// --- AP Mode Settings (for initial setup) ---
#define AP_SSID       "PrintServer-Setup"
#define AP_PASSWORD   "12345678"

// --- Print Server Settings ---
#define PRINTER_NAME       "Epson_EPL5900_WiFi"
#define RAW_SOCKET_PORT    9100    // JetDirect/AppSocket protocol port
#define WEB_SERVER_PORT    80      // Web management UI port
#define MDNS_HOSTNAME      "epl5900"  // Accessible via epl5900.local

// --- USB Host Settings ---
#define USB_HOST_TASK_PRIORITY    5
#define USB_HOST_TASK_STACK_SIZE  4096
#define PRINT_BUFFER_SIZE         (512 * 1024)  // 512KB print buffer (in PSRAM)

// --- Status LED (optional) ---
#define LED_PIN           38       // On-board LED (adjust for your board)
#define LED_WIFI_OK       1       // WiFi connected
#define LED_PRINTING      2       // Printing
#define LED_ERROR         3       // Error

// --- OTA Update ---
#define OTA_HOSTNAME      "printserver"
#define OTA_PASSWORD      ""      // Empty = no password required

// --- Watchdog Timer ---
#define WDT_TIMEOUT_SEC   30      // Watchdog timeout in seconds

// --- NVS Storage Keys ---
#define NVS_NAMESPACE     "printsvr"
#define NVS_KEY_SSID      "wifi_ssid"
#define NVS_KEY_PASS      "wifi_pass"
#define NVS_KEY_HOSTNAME  "hostname"

#endif // CONFIG_H
