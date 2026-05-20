#ifndef USB_PRINTER_H
#define USB_PRINTER_H

#include <Arduino.h>
#include "usb/usb_host.h"

// USB Printer Class 定義
#define USB_CLASS_PRINTER       0x07
#define USB_SUBCLASS_PRINTER    0x01
#define USB_PROTOCOL_UNIDIR    0x01  // 單向
#define USB_PROTOCOL_BIDIR     0x02  // 雙向
#define USB_PROTOCOL_1284      0x03  // IEEE 1284.4

// USB Printer Class 請求
#define USB_PRINTER_GET_DEVICE_ID   0x00
#define USB_PRINTER_GET_PORT_STATUS 0x01
#define USB_PRINTER_SOFT_RESET     0x02

// 印表機狀態
enum PrinterState {
  PRINTER_DISCONNECTED,
  PRINTER_CONNECTED,
  PRINTER_READY,
  PRINTER_BUSY,
  PRINTER_ERROR
};

class UsbPrinter {
public:
  UsbPrinter();

  // 初始化 USB Host
  bool begin();

  // 主迴圈處理（需在 loop 或 task 中持續呼叫）
  void task();

  // 傳送資料到印表機
  int sendData(const uint8_t* data, size_t length);

  // 取得印表機狀態
  PrinterState getState() const { return _state; }
  bool isConnected() const { return _state >= PRINTER_CONNECTED; }
  bool isReady() const { return _state == PRINTER_READY; }

  // 取得印表機資訊
  String getDeviceId() const { return _deviceId; }
  String getManufacturer() const { return _manufacturer; }
  String getProduct() const { return _product; }

  // 軟體重置
  bool softReset();

private:
  PrinterState _state;
  String _deviceId;
  String _manufacturer;
  String _product;

  // USB Host 相關
  usb_host_client_handle_t _clientHandle;
  usb_device_handle_t _deviceHandle;
  uint8_t _epOut;      // Bulk OUT 端點位址
  uint8_t _epIn;       // Bulk IN 端點位址
  uint16_t _epOutMPS;  // Bulk OUT 最大封包大小
  uint8_t _ifaceNum;   // 介面編號

  // 內部方法
  static void _clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg);
  void _handleClientEvent(const usb_host_client_event_msg_t* eventMsg);
  bool _openDevice(uint8_t devAddr);
  bool _findPrinterInterface();
  bool _claimInterface();
  void _closeDevice();
  bool _getDeviceId();
};

#endif // USB_PRINTER_H
