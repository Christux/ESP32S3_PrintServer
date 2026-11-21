#ifndef WEB_UI_H
#define WEB_UI_H

#include <Arduino.h>
#include <WebServer.h>
#include "wifi_manager.h"
#include "usb_printer.h"
#include "print_server.h"
#include "config.h"

/** Provides the HTTP management interface and its JSON API. */
class WebUI {
public:
  /**
   * Constructs the web interface for the supplied system components.
   *
   * @param wifi WiFi manager whose state and credentials are exposed.
   * @param printer Printer interface whose status is reported.
   * @param printSvr Print server whose jobs and statistics are reported.
   */
  WebUI(WiFiManager& wifi, UsbPrinter& printer, PrintServer& printSvr);

  /** Registers HTTP routes and starts the management server. */
  void begin();

  /** Processes pending HTTP requests. Call continuously from loop(). */
  void task();

private:
  WebServer _server;
  WiFiManager& _wifi;
  UsbPrinter& _printer;
  PrintServer& _printSvr;

  /** Serves the management page. */
  void _handleRoot();

  /** Returns printer, WiFi, job, and system status as JSON. */
  void _handleStatus();

  /** Scans for WiFi networks and returns the results as JSON. */
  void _handleWifiScan();

  /** Applies WiFi credentials submitted through the HTTP API. */
  void _handleWifiConnect();

  /** Restarts the ESP32 after sending an acknowledgment. */
  void _handleRestart();

  /** Returns an HTTP 404 response for unknown routes. */
  void _handleNotFound();

  /** Generates the management interface HTML. */
  String _generatePage();
};

#endif // WEB_UI_H
