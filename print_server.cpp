#include "print_server.h"

PrintServer::PrintServer(UsbPrinter& printer)
  : _printer(printer),
    _server(RAW_SOCKET_PORT),
    _totalJobs(0), _totalBytes(0),
    _buffer(nullptr), _bufferSize(0) {

  _currentJob = {false, 0, 0, 0, ""};
}

bool PrintServer::begin() {
  // 在 PSRAM 中配置列印緩衝區
  _bufferSize = PRINT_BUFFER_SIZE;
  _buffer = (uint8_t*)ps_malloc(_bufferSize);

  if (_buffer == nullptr) {
    // 如果 PSRAM 不可用，使用較小的 heap 緩衝區
    _bufferSize = 8192;
    _buffer = (uint8_t*)malloc(_bufferSize);
    Serial.println("[Print] ⚠ PSRAM 不可用，使用 8KB 緩衝區");
  } else {
    Serial.printf("[Print] ✓ 已在 PSRAM 配置 %dKB 緩衝區\n", _bufferSize / 1024);
  }

  if (_buffer == nullptr) {
    Serial.println("[Print] ✗ 無法配置緩衝區！");
    return false;
  }

  _server.begin();
  _server.setNoDelay(true);
  Serial.printf("[Print] ✓ Raw Socket 伺服器已啟動，埠: %d\n", RAW_SOCKET_PORT);
  return true;
}

void PrintServer::task() {
  // 檢查是否有新的連線
  if (_server.hasClient()) {
    if (_client && _client.connected()) {
      // 已有列印工作在進行，拒絕新連線
      WiFiClient rejected = _server.accept();
      Serial.printf("[Print] 拒絕來自 %s 的連線（印表機忙碌中）\n",
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
        Serial.printf("[Print] ← 新列印工作，來自 %s\n", _currentJob.clientIP.c_str());
      }
    }
  }

  // 處理進行中的列印工作
  if (_client && _client.connected()) {
    _handleClient();
  } else if (_currentJob.active) {
    // 客戶端已斷開，工作完成
    unsigned long elapsed = millis() - _currentJob.startTime;
    Serial.printf("[Print] ✓ 列印工作完成: %d bytes, 耗時 %lums\n",
                  _currentJob.bytesReceived, elapsed);
    _totalJobs++;
    _totalBytes += _currentJob.bytesReceived;
    _currentJob.active = false;
    _client.stop();
  }
}

void PrintServer::_handleClient() {
  if (!_client.available()) return;

  // 檢查印表機是否就緒
  if (!_printer.isReady() && !_printer.isConnected()) {
    Serial.println("[Print] ✗ 印表機未連接，丟棄資料");
    // 讀取並丟棄資料
    while (_client.available()) {
      _client.read(_buffer, min((size_t)_client.available(), _bufferSize));
    }
    return;
  }

  // 讀取資料並轉發到印表機
  while (_client.available()) {
    size_t available = _client.available();
    size_t toRead = min(available, _bufferSize);
    size_t bytesRead = _client.read(_buffer, toRead);

    if (bytesRead > 0) {
      _currentJob.bytesReceived += bytesRead;

      // 傳送到 USB 印表機
      int sent = _printer.sendData(_buffer, bytesRead);
      if (sent > 0) {
        _currentJob.bytesSent += sent;
      } else {
        Serial.printf("[Print] ⚠ USB 傳送失敗，已接收 %d bytes\n",
                      _currentJob.bytesReceived);
      }

      // 每 100KB 報告一次進度
      if (_currentJob.bytesReceived % (100 * 1024) < bytesRead) {
        Serial.printf("[Print] 進度: %dKB 接收 / %dKB 已送\n",
                      _currentJob.bytesReceived / 1024,
                      _currentJob.bytesSent / 1024);
      }
    }

    // 讓出 CPU 給其他任務
    yield();
  }
}
