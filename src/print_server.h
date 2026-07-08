#ifndef PRINT_SERVER_H
#define PRINT_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include "usb_printer.h"
#include "config.h"

// Print job status
struct PrintJob {
  bool active;
  size_t bytesReceived;
  size_t bytesSent;
  unsigned long startTime;
  String clientIP;
};

class PrintServer {
public:
  PrintServer(UsbPrinter& printer);

  // Start Raw Socket server (Port 9100)
  bool begin();

  // Main loop processing
  void task();

  // Get current print job info
  PrintJob getCurrentJob() const { return _currentJob; }
  uint32_t getTotalJobs() const { return _totalJobs; }
  uint32_t getTotalBytes() const { return _totalBytes; }

private:
  UsbPrinter& _printer;
  WiFiServer _server;
  WiFiClient _client;
  PrintJob _currentJob;

  uint32_t _totalJobs;
  uint32_t _totalBytes;

  // PSRAM buffer
  uint8_t* _buffer;
  size_t _bufferSize;

  void _handleClient();
};

#endif // PRINT_SERVER_H
