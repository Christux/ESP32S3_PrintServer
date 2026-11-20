// ============================================================
//  ESP32-S3 WiFi Print Server
//  Print from Mac to Brother HL-2030 over WiFi
//
//  Hardware: ESP32-S3-WROOM-1-N8R8 (8MB Flash / 8MB PSRAM)
//  Architecture: ESP32-S3 USB Host → Printer
//                WiFi Raw Socket (Port 9100) ← Mac
//                mDNS auto-discovery + Web UI + OTA
//
//  Author: MassTang
//  Date: 2026-05
// ============================================================

#include <WiFi.h>
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

// --- USB Host Background Task ---
TaskHandle_t usbTaskHandle = NULL;

void usbHostTask(void* param) {
  Serial.println("[Task] USB Host task started");

  while (true) {
    usbPrinter.task();
    vTaskDelay(pdMS_TO_TICKS(10));  // 10ms interval
  }
}

// --- Setup mDNS ---
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
void setupOTA() {
  ArduinoOTA.setHostname(OTA_HOSTNAME);

  if (strlen(OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    String type = (ArduinoOTA.getCommand() == U_FLASH)
                  ? "sketch" : "filesystem";
    Serial.printf("[OTA] Starting %s update\n", type.c_str());
  });

  ArduinoOTA.onEnd([]() {
    Serial.println("\n[OTA] Update complete!");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("[OTA] Progress: %u%%\r", (progress / (total / 100)));
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
  });

  ArduinoOTA.begin();
  Serial.println("[OTA] ✓ OTA update enabled");
}

// --- LED Status Indicator ---
void setLED(int mode) {
  switch (mode) {
    case LED_WIFI_OK:
      digitalWrite(LED_PIN, HIGH);
      break;
    case LED_PRINTING:
      // Blink effect handled in loop
      break;
    case LED_ERROR:
      digitalWrite(LED_PIN, LOW);
      break;
  }
}

// ============================================================
//  setup()
// ============================================================
void setup() {
  delay(1000);
  Serial.begin(460800);
  delay(1000);

  Serial.println();
  Serial.println("╔══════════════════════════════════════╗");
  Serial.println("║   ESP32-S3 WiFi Print Server v1.0   ║");
  Serial.println("║   Brother HL-2030                    ║");
  Serial.println("╚══════════════════════════════════════╝");
  Serial.println();

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

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
  esp_task_wdt_init(&wdtConfig);
  esp_task_wdt_add(NULL);

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
void loop() {
  // Feed watchdog
  esp_task_wdt_reset();

  // WiFi management (auto-reconnect)
  wifiMgr.task();

  // Print server
  printSvr.task();

  // Web management UI
  webUI.task();

  // OTA update check
  if (wifiMgr.isConnected()) {
    ArduinoOTA.handle();
  }

  // LED blink (during printing)
  static unsigned long lastBlink = 0;
  if (printSvr.getCurrentJob().active) {
    if (millis() - lastBlink > 200) {
      lastBlink = millis();
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    }
  }

  // Short delay to avoid CPU spinning at full speed
  delay(1);
}
