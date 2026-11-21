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

/** State of the USB host printer interface. */
enum PrinterState {
  PRINTER_DISCONNECTED,
  PRINTER_CONNECTED,
  PRINTER_READY,
  PRINTER_BUSY,
  PRINTER_ERROR
};

/** Manages USB host discovery and bulk data transfer to a printer. */
class UsbPrinter {
public:
  /** Constructs a disconnected USB printer interface. */
  UsbPrinter();

  /**
   * Initializes the ESP32 USB host library and printer event client.
   *
   * @return true if USB host initialization succeeds, false otherwise.
   */
  bool begin();

  /** Processes USB host events. Call continuously from loop() or task(). */
  void task();

  /**
   * Sends print data through the printer's bulk OUT endpoint.
   *
   * @param data Pointer to the bytes to send.
   * @param length Number of bytes in data.
   * @return Number of bytes sent, or a negative value on transfer failure.
   */
  int sendData(const uint8_t* data, size_t length);

  /** Returns the current printer state. */
  PrinterState getState() const { return _state; }

  /** Reports whether a USB printer device is connected. */
  bool isConnected() const { return _state >= PRINTER_CONNECTED; }

  /** Reports whether the printer is ready to receive data. */
  bool isReady() const { return _state == PRINTER_READY; }

  /** Returns the printer IEEE 1284 device ID. */
  String getDeviceId() const { return _deviceId; }

  /** Returns the detected printer manufacturer. */
  String getManufacturer() const { return _manufacturer; }

  /** Returns the detected printer product name. */
  String getProduct() const { return _product; }

  /**
   * Sends the USB Printer Class soft-reset control request.
   *
   * @return true if the reset request succeeds, false otherwise.
   */
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

  /** Receives USB host client events and forwards them to this instance. */
  static void _clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg);

  /** Handles new-device and disconnected-device events. */
  void _handleClientEvent(const usb_host_client_event_msg_t* eventMsg);

  /** Opens a detected USB device and initializes its printer interface. */
  bool _openDevice(uint8_t devAddr);

  /** Finds the printer interface and its bulk endpoints. */
  bool _findPrinterInterface();

  /** Claims the detected printer interface for exclusive use. */
  bool _claimInterface();

  /** Releases resources and marks the printer disconnected. */
  void _closeDevice();

  /** Requests and stores the printer IEEE 1284 device ID. */
  bool _getDeviceId();
};

#endif // USB_PRINTER_H
