// ============================================================
//  ESP32-S3 WiFi Print Server
//  Print from Mac, Windows or Linux to Epson EPL-5900 over WiFi
//
//  Hardware: ESP32-S3-N16R8 (16MB Flash / 8MB PSRAM)
//  Architecture: ESP32-S3 USB Host → Printer
//                WiFi Raw Socket (Port 9100) ← Mac
//                mDNS auto-discovery + Web UI + OTA
//
//  Author: MassTang
//  Date: 2026-05
// ============================================================

#include <WiFi.h>
#include <FastLED.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <esp_task_wdt.h>

#include "config.h"
#include "usb_printer.h"
#include "wifi_manager.h"
#include "print_server.h"
#include "web_ui.h"

// --- Global Objects ---
UsbPrinter   usbPrinter;
WiFiManager  wifiMgr;
PrintServer  printSvr(usbPrinter);
WebUI        webUI(wifiMgr, usbPrinter, printSvr);
CRGB         statusLED[1];
constexpr unsigned long PRINTING_LED_MIN_DURATION_MS = 10000;

// --- USB Host Background Task ---
TaskHandle_t usbTaskHandle = NULL;
bool networkServicesStarted = false;

/**
 * Runs USB host event processing on a dedicated background task.
 *
 * @param param Unused task argument required by the FreeRTOS task signature.
 */
void usbHostTask(void* param) {
  Serial.println("[Task] USB Host task started");

  while (true) {
    usbPrinter.task();
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms interval
  }
}

// --- Setup mDNS ---
/** Registers the raw print socket and web management services with mDNS. */
void setupMDNS() {
  if (MDNS.begin(MDNS_HOSTNAME)) {
    // Advertise Raw Socket print service
    MDNS.addService("pdl-datastream", "tcp", RAW_SOCKET_PORT);

    // Advertise HTTP management UI
    MDNS.addService("http", "tcp", WEB_SERVER_PORT);

    Serial.printf("[mDNS] ✓ Registered %s.local\n", MDNS_HOSTNAME);
    Serial.printf("[mDNS]   Print: socket://%s.local:%d\n", MDNS_HOSTNAME, RAW_SOCKET_PORT);
    Serial.printf("[mDNS]   Manage: http://%s.local\n", MDNS_HOSTNAME);
  } else {
    Serial.println("[mDNS] ✗ Failed to start");
  }
}

// --- Setup OTA ---
/**
 * Sets the LED indication for a status mode.
 *
 * @param mode One of the LED status constants.
 */
void setLED(int mode);

/** Configures and starts Arduino OTA updates for the application. */
void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);

  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH)
                  ? "sketch" : "filesystem";
    setLED(LED_OTA);
    Serial.printf("[OTA] Starting %s update\n", type.c_str());
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update complete!");
    setLED(wifiMgr.isConnected() ? LED_WIFI_OK : LED_ERROR);
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    // ArduinoOTA handles the whole transfer synchronously inside loop().
    esp_task_wdt_reset();
    if (total > 0) {
      Serial.printf("[OTA] Progress: %u%%\r", (progress * 100) / total);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("[OTA] Error [%u]: ", error);
    switch (error) {
      case OTA_AUTH_ERROR:    Serial.println("Authentication failed"); break;
      case OTA_BEGIN_ERROR:   Serial.println("Start failed"); break;
      case OTA_CONNECT_ERROR: Serial.println("Connection failed"); break;
      case OTA_RECEIVE_ERROR: Serial.println("Receive failed"); break;
      case OTA_END_ERROR:     Serial.println("End failed"); break;
    }
    setLED(wifiMgr.isConnected() ? LED_WIFI_OK : LED_ERROR);
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] ✓ OTA update enabled");
}

// --- LED Status Indicator ---
/**
 * Updates the status LED to reflect the requested system mode.
 *
 * @param mode One of LED_WIFI_OK, LED_PRINTING, LED_OTA, or LED_ERROR.
 */
void setLED(int mode) {
  switch (mode) {
    case LED_WIFI_OK:
      statusLED[0] = CRGB::Green;
      break;
    case LED_PRINTING:
      statusLED[0] = CRGB::Blue;
      break;
    case LED_OTA:
      statusLED[0] = CRGB::Purple;
      break;
    case LED_ERROR:
      statusLED[0] = CRGB::Red;
      break;
    default:
      statusLED[0] = CRGB::Black;
      break;
  }
  FastLED.show();
}

/** Updates the printing indication while preserving its configured minimum duration. */
void handle_led() {
  static bool printingLEDActive = false;
  static unsigned long printingLEDStartedAt = 0;
  const bool isPrinting = printSvr.getCurrentJob().active;

  if (isPrinting && !printingLEDActive) {
    setLED(LED_PRINTING);
    printingLEDActive = true;
    printingLEDStartedAt = millis();
  } else if (printingLEDActive && !isPrinting &&
             millis() - printingLEDStartedAt >= PRINTING_LED_MIN_DURATION_MS) {
    setLED(wifiMgr.isConnected() ? LED_WIFI_OK : LED_ERROR);
    printingLEDActive = false;
  }
}

// ============================================================
//  setup()
// ============================================================
/** Initializes hardware, network services, print services, watchdog, and status output. */
void setup() {
  delay(1000);
  Serial.begin(460800);
  delay(1000);

  Serial.println();
  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║   ESP32-S3 WiFi Print Server v1.0   ║");
  Serial.println("║   Epson EPL-5900                     ║");
  Serial.println("╚══════════════════════════════════════╝");
  Serial.println();

  // RGB status LED
  FastLED.addLeds<WS2812B, LED_PIN, GRB>(statusLED, 1);
  FastLED.setBrightness(16);
  FastLED.clear(true);

  // Display memory info
  Serial.printf("[SYS] Free Heap:  %d KB\n", ESP.getFreeHeap() / 1024);
  Serial.printf("[SYS] Free PSRAM: %d KB\n", ESP.getFreePsram() / 1024);
  Serial.printf("[SYS] Flash Size: %d MB\n", ESP.getFlashChipSize() / (1024 * 1024));
  Serial.println();

  // 1. Initialize USB Host
  Serial.println("=== Initializing USB Host ===");
  if (usbPrinter.begin()) {
    // Create USB Host background task on core 1
    xTaskCreatePinnedToCore(
      usbHostTask,
      "usb_host",
      USB_HOST_TASK_STACK_SIZE,
      NULL,
      USB_HOST_TASK_PRIORITY,
      &usbTaskHandle,
      1  // Core 1
    );
  } else {
    Serial.println("[USB] ✗ USB Host initialization failed! Check wiring");
  }
  Serial.println();

  // 2. Initialize WiFi
  Serial.println("=== Initializing WiFi ===");
  wifiMgr.begin();
  Serial.println();

  // 3. If WiFi is connected, start network services
  if (wifiMgr.isConnected()) {
    setupMDNS();
    setupOTA();
    networkServicesStarted = true;
  }

  // 4. Start print server
  Serial.println("=== Initializing Print Server ===");
  printSvr.begin();
  Serial.println();

  // 5. Start Web management UI
  Serial.println("=== Initializing Web Management UI ===");
  webUI.begin();
  Serial.println();

  // 6. Start watchdog timer
  esp_task_wdt_config_t wdtConfig = {
    .timeout_ms = WDT_TIMEOUT_SEC * 1000,
    .idle_core_mask = (1 << CONFIG_FREERTOS_NUMBER_OF_CORES) - 1,
    .trigger_panic = true
  };
  esp_err_t wdtStatus = esp_task_wdt_reconfigure(&wdtConfig);
  if (wdtStatus == ESP_ERR_INVALID_STATE) {
    wdtStatus = esp_task_wdt_init(&wdtConfig);
  }
  if (wdtStatus != ESP_OK) {
    Serial.printf("[WDT] Configuration failed: %s\n", esp_err_to_name(wdtStatus));
  } else {
    wdtStatus = esp_task_wdt_add(NULL);
    if (wdtStatus == ESP_ERR_INVALID_STATE) {
      Serial.println("[WDT] loopTask is already subscribed");
    } else if (wdtStatus != ESP_OK) {
      Serial.printf("[WDT] Failed to subscribe loopTask: %s\n",
                    esp_err_to_name(wdtStatus));
    }
  }

  // Done
  Serial.println("════════════════════════════════════════");
  Serial.println("  System ready!");
  if (wifiMgr.isConnected()) {
    Serial.printf("  Print to: socket://%s:%d\n", wifiMgr.getIP().c_str(), RAW_SOCKET_PORT);
    Serial.printf("  Manage: http://%s.local\n", MDNS_HOSTNAME);
  } else {
    Serial.printf("  Connect to WiFi '%s' for setup\n", AP_SSID);
    Serial.printf("  Setup page: http://%s\n", wifiMgr.getIP().c_str());
  }
  Serial.println("════════════════════════════════════════");

  setLED(wifiMgr.isConnected() ? LED_WIFI_OK : LED_ERROR);
}

// ============================================================
//  loop()
// ============================================================
/** Runs ongoing WiFi, print, web, OTA, watchdog, and status processing. */
void loop() {
  // Feed watchdog
  esp_task_wdt_reset();

  // WiFi management (auto-reconnect)
  wifiMgr.task();

  // Start network services when WiFi is configured after booting in AP mode.
  if (wifiMgr.isConnected() && !networkServicesStarted) {
    setupMDNS();
    setupOTA();
    networkServicesStarted = true;
  }

  // Print server
  printSvr.task();

  // Web management UI
  webUI.task();

  // OTA update check
  if (wifiMgr.isConnected()) {
    ArduinoOTA.handle();
  }

  handle_led();

  // Short delay to avoid CPU spinning at full speed
  delay(1);
}
