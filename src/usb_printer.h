#ifndef USB_PRINTER_H
#define USB_PRINTER_H

#include <Arduino.h>
#include "usb/usb_host.h"

// USB Printer Class definitions
#define USB_CLASS_PRINTER       0x07
#define USB_SUBCLASS_PRINTER    0x01
#define USB_PROTOCOL_UNIDIR    0x01  // Unidirectional
#define USB_PROTOCOL_BIDIR     0x02  // Bidirectional
#define USB_PROTOCOL_1284      0x03  // IEEE 1284.4

// USB Printer Class requests
#define USB_PRINTER_GET_DEVICE_ID   0x00
#define USB_PRINTER_GET_PORT_STATUS 0x01
#define USB_PRINTER_SOFT_RESET     0x02

// Printer state
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

  // Initialize USB Host
  bool begin();

  // Main loop processing (must be called continuously in loop or task)
  void task();

  // Send data to printer
  int sendData(const uint8_t* data, size_t length);

  // Get printer state
  PrinterState getState() const { return _state; }
  bool isConnected() const { return _state >= PRINTER_CONNECTED; }
  bool isReady() const { return _state == PRINTER_READY; }

  // Get printer info
  String getDeviceId() const { return _deviceId; }
  String getManufacturer() const { return _manufacturer; }
  String getProduct() const { return _product; }

  // Software reset
  bool softReset();

private:
  PrinterState _state;
  String _deviceId;
  String _manufacturer;
  String _product;

  // USB Host related
  usb_host_client_handle_t _clientHandle;
  usb_device_handle_t _deviceHandle;
  uint8_t _epOut;      // Bulk OUT endpoint address
  uint8_t _epIn;       // Bulk IN endpoint address
  uint16_t _epOutMPS;  // Bulk OUT max packet size
  uint8_t _ifaceNum;   // Interface number

  // Internal methods
  static void _clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg);
  void _handleClientEvent(const usb_host_client_event_msg_t* eventMsg);
  bool _openDevice(uint8_t devAddr);
  bool _findPrinterInterface();
  bool _claimInterface();
  void _closeDevice();
  bool _getDeviceId();
};

#endif // USB_PRINTER_H
