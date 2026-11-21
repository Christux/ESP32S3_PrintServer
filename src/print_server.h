#ifndef PRINT_SERVER_H
#define PRINT_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include "usb_printer.h"

/** Snapshot of the print job currently handled by the server. */
struct PrintJob {
  /** true while a client connection is actively serving a print job. */
  bool active;

  /** Number of bytes received from the client. */
  size_t bytesReceived;

  /** Number of bytes forwarded to the USB printer. */
  size_t bytesSent;

  /** Millisecond timestamp at which the job started. */
  unsigned long startTime;

  /** IP address of the client that opened the print job. */
  String clientIP;
};

/** Accepts raw TCP print data and forwards it to a USB printer. */
class PrintServer {
public:
  /**
   * Constructs the raw socket print server.
   *
   * @param printer USB printer to receive incoming print data.
   */
  PrintServer(UsbPrinter& printer);

  /**
   * Allocates the transfer buffer and starts the raw socket listener.
   *
   * @return true if the buffer and server start successfully, false otherwise.
   */
  bool begin();

  /** Processes connections and active print data. Call continuously from loop(). */
  void task();

  /** Returns a snapshot of the current print job. */
  PrintJob getCurrentJob() const { return _currentJob; }

  /** Returns the number of completed print jobs. */
  uint32_t getTotalJobs() const { return _totalJobs; }

  /** Returns the total number of bytes received from clients. */
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

  /** Accepts and forwards available bytes from the active client. */
  void _handleClient();
};

#endif // PRINT_SERVER_H
