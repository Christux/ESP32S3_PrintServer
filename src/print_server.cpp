#include "config.h"
#include "print_server.h"

PrintServer::PrintServer(UsbPrinter& printer)
  : _printer(printer),
    _server(RAW_SOCKET_PORT),
    _totalJobs(0), _totalBytes(0),
    _buffer(nullptr), _bufferSize(0) {

  _currentJob = {false, 0, 0, 0, ""};
}

bool PrintServer::begin() {
  // Allocate print buffer in PSRAM
  _bufferSize = PRINT_BUFFER_SIZE;
  _buffer = (uint8_t*)ps_malloc(_bufferSize);

  if (_buffer == nullptr) {
    // If PSRAM is unavailable, use a smaller heap buffer
    _bufferSize = 8192;
    _buffer = (uint8_t*)malloc(_bufferSize);
    Serial.println("[Print] ⚠ PSRAM unavailable, using 8KB heap buffer");
  } else {
    Serial.printf("[Print] ✓ Allocated %dKB buffer in PSRAM\n", _bufferSize / 1024);
  }

  if (_buffer == nullptr) {
    Serial.println("[Print] ✗ Failed to allocate buffer!");
    return false;
  }

  _server.begin();
  _server.setNoDelay(true);
  Serial.printf("[Print] ✓ Raw Socket server started on port: %d\n", RAW_SOCKET_PORT);
  return true;
}

void PrintServer::task() {
  // Check for new connections
  if (_server.hasClient()) {
    if (_client && _client.connected()) {
      // Print job in progress, reject new connection
      WiFiClient rejected = _server.accept();
      Serial.printf("[Print] Rejected connection from %s (printer busy)\n",
                    rejected.remoteIP().toString().c_str());
      rejected.stop();
    } else {
      _client = _server.accept();
      if (_client) {
        _currentJob = {
          true,
          0, 0,
          millis(),
          _client.remoteIP().toString()
        };
        Serial.printf("[Print] ← New print job from %s\n", _currentJob.clientIP.c_str());
      }
    }
  }

  // Process active print job
  if (_client && _client.connected()) {
    _handleClient();
  } else if (_currentJob.active) {
    // Client disconnected, job complete
    unsigned long elapsed = millis() - _currentJob.startTime;
    Serial.printf("[Print] ✓ Print job complete: %d bytes, elapsed %lums\n",
                  _currentJob.bytesReceived, elapsed);
    _totalJobs++;
    _totalBytes += _currentJob.bytesReceived;
    _currentJob.active = false;
    _client.stop();
  }
}

void PrintServer::_handleClient() {
  if (!_client.available()) return;

  // Check if printer is ready
  if (!_printer.isReady() && !_printer.isConnected()) {
    Serial.println("[Print] ✗ Printer not connected, discarding data");
    // Read and discard data
    while (_client.available()) {
      _client.read(_buffer, min((size_t)_client.available(), _bufferSize));
    }
    return;
  }

  // Read data and forward to printer
  while (_client.available()) {
    size_t available = _client.available();
    size_t toRead = min(available, _bufferSize);
    size_t bytesRead = _client.read(_buffer, toRead);

    if (bytesRead > 0) {
      _currentJob.bytesReceived += bytesRead;

      // Send to USB printer
      int sent = _printer.sendData(_buffer, bytesRead);
      if (sent > 0) {
        _currentJob.bytesSent += sent;
      } else {
        Serial.printf("[Print] ⚠ USB send failed, received %d bytes\n",
                      _currentJob.bytesReceived);
      }

      // Report progress every 100KB
      if (_currentJob.bytesReceived % (100 * 1024) < bytesRead) {
        Serial.printf("[Print] Progress: %dKB received / %dKB sent\n",
                      _currentJob.bytesReceived / 1024,
                      _currentJob.bytesSent / 1024);
      }
    }

    // Yield CPU to other tasks
    yield();
  }
}
