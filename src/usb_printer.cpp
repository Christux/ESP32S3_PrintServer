#include "usb_printer.h"

static const char* TAG = "USB_PRINTER";

UsbPrinter::UsbPrinter()
  : _state(PRINTER_DISCONNECTED),
    _clientHandle(nullptr),
    _deviceHandle(nullptr),
    _epOut(0), _epIn(0), _epOutMPS(64),
    _ifaceNum(0) {}

bool UsbPrinter::begin() {
  // Install USB Host Library
  usb_host_config_t hostConfig = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK) {
    Serial.printf("[USB] Host install failed: %s\n", esp_err_to_name(err));
    return false;
  }

  // Create USB Host Client
  usb_host_client_config_t clientConfig = {
    .is_synchronous = false,
    .max_num_event_msg = 5,
    .async = {
      .client_event_callback = _clientEventCallback,
      .callback_arg = this,
    }
  };

  err = usb_host_client_register(&clientConfig, &_clientHandle);
  if (err != ESP_OK) {
    Serial.printf("[USB] Client register failed: %s\n", esp_err_to_name(err));
    return false;
  }

  Serial.println("[USB] Host initialized, waiting for printer connection...");
  return true;
}

void UsbPrinter::task() {
  if (_clientHandle == nullptr) return;

  // Handle USB Host Library events
  usb_host_lib_handle_events(0, NULL);

  // Handle Client events
  usb_host_client_handle_events(_clientHandle, 0);
}

void UsbPrinter::_clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg) {
  UsbPrinter* self = (UsbPrinter*)arg;
  self->_handleClientEvent(eventMsg);
}

void UsbPrinter::_handleClientEvent(const usb_host_client_event_msg_t* eventMsg) {
  switch (eventMsg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      Serial.printf("[USB] New device detected at address: %d\n", eventMsg->new_dev.address);
      if (_state == PRINTER_DISCONNECTED) {
        _openDevice(eventMsg->new_dev.address);
      }
      break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      Serial.println("[USB] Device disconnected");
      _closeDevice();
      break;

    default:
      break;
  }
}

bool UsbPrinter::_openDevice(uint8_t devAddr) {
  esp_err_t err = usb_host_device_open(_clientHandle, devAddr, &_deviceHandle);
  if (err != ESP_OK) {
    Serial.printf("[USB] Failed to open device: %s\n", esp_err_to_name(err));
    return false;
  }

  _state = PRINTER_CONNECTED;

  // Get device descriptor
  const usb_device_desc_t* devDesc;
  err = usb_host_get_device_descriptor(_deviceHandle, &devDesc);
  if (err == ESP_OK) {
    Serial.printf("[USB] VID: 0x%04X, PID: 0x%04X\n", devDesc->idVendor, devDesc->idProduct);
  }

  // Use VID/PID for identification (string descriptors require additional control transfer)
  if (devDesc->idVendor == 0x04B8) {
    _manufacturer = "Epson";
    if (devDesc->idProduct == 0x0005) {
      _product = "EPL-5900";
    } else {
      _product = "Epson Printer";
    }
  } else if (devDesc->idVendor == 0x04F9) {
    _manufacturer = "Brother";
    if (devDesc->idProduct == 0x0027) {
      _product = "HL-2030 series";
    } else {
      _product = "Brother Printer";
    }
  } else {
    _manufacturer = String("VID:0x") + String(devDesc->idVendor, HEX);
    _product = String("PID:0x") + String(devDesc->idProduct, HEX);
  }
  Serial.printf("[USB] Manufacturer: %s\n", _manufacturer.c_str());
  Serial.printf("[USB] Product: %s\n", _product.c_str());

  // Find Printer interface
  if (!_findPrinterInterface()) {
    Serial.println("[USB] No Printer interface found, not a printer");
    _closeDevice();
    return false;
  }

  // Claim interface
  if (!_claimInterface()) {
    Serial.println("[USB] Failed to claim printer interface");
    _closeDevice();
    return false;
  }

  // Get Device ID
  _getDeviceId();

  _state = PRINTER_READY;
  Serial.println("[USB] ✓ Printer ready");
  return true;
}

bool UsbPrinter::_findPrinterInterface() {
  const usb_config_desc_t* configDesc;
  esp_err_t err = usb_host_get_active_config_descriptor(_deviceHandle, &configDesc);
  if (err != ESP_OK) {
    Serial.printf("[USB] Failed to get configuration descriptor: %s\n", esp_err_to_name(err));
    return false;
  }

  // Iterate all interface descriptors to find Printer Class
  int offset = 0;
  const usb_intf_desc_t* ifaceDesc = nullptr;
  uint16_t totalLen = configDesc->wTotalLength;
  const uint8_t* p = (const uint8_t*)configDesc;

  while (offset < totalLen) {
    const usb_standard_desc_t* desc = (const usb_standard_desc_t*)(p + offset);
    if (desc->bLength == 0) break;

    if (desc->bDescriptorType == USB_B_DESCRIPTOR_TYPE_INTERFACE) {
      ifaceDesc = (const usb_intf_desc_t*)desc;

      if (ifaceDesc->bInterfaceClass == USB_CLASS_PRINTER &&
          ifaceDesc->bInterfaceSubClass == USB_SUBCLASS_PRINTER) {

        _ifaceNum = ifaceDesc->bInterfaceNumber;
        Serial.printf("[USB] Found Printer interface #%d, Protocol: %d\n",
                      _ifaceNum, ifaceDesc->bInterfaceProtocol);

        // Find endpoints for this interface
        int epOffset = offset + desc->bLength;
        for (int ep = 0; ep < ifaceDesc->bNumEndpoints && epOffset < totalLen; ep++) {
          const usb_standard_desc_t* epDescRaw = (const usb_standard_desc_t*)(p + epOffset);
          if (epDescRaw->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const usb_ep_desc_t* epDesc = (const usb_ep_desc_t*)epDescRaw;

            if ((epDesc->bmAttributes & 0x03) == USB_BM_ATTRIBUTES_XFER_BULK) {
              if (epDesc->bEndpointAddress & 0x80) {
                // IN endpoint
                _epIn = epDesc->bEndpointAddress;
                Serial.printf("[USB] Bulk IN endpoint: 0x%02X\n", _epIn);
              } else {
                // OUT endpoint
                _epOut = epDesc->bEndpointAddress;
                _epOutMPS = epDesc->wMaxPacketSize;
                Serial.printf("[USB] Bulk OUT endpoint: 0x%02X, MPS: %d\n", _epOut, _epOutMPS);
              }
            }
          }
          epOffset += epDescRaw->bLength;
        }

        return (_epOut != 0);  // At least need an OUT endpoint
      }
    }
    offset += desc->bLength;
  }

  return false;
}

bool UsbPrinter::_claimInterface() {
  esp_err_t err = usb_host_interface_claim(_clientHandle, _deviceHandle, _ifaceNum, 0);
  if (err != ESP_OK) {
    Serial.printf("[USB] Failed to claim interface: %s\n", esp_err_to_name(err));
    return false;
  }
  Serial.printf("[USB] Claimed interface #%d\n", _ifaceNum);
  return true;
}

void UsbPrinter::_closeDevice() {
  if (_deviceHandle != nullptr) {
    usb_host_interface_release(_clientHandle, _deviceHandle, _ifaceNum);
    usb_host_device_close(_clientHandle, _deviceHandle);
    _deviceHandle = nullptr;
  }
  _state = PRINTER_DISCONNECTED;
  _epOut = 0;
  _epIn = 0;
  _deviceId = "";
  _manufacturer = "";
  _product = "";
  Serial.println("[USB] Device closed");
}

bool UsbPrinter::_getDeviceId() {
  if (_deviceHandle == nullptr) return false;

  // USB Printer Class: GET_DEVICE_ID request
  uint8_t buf[256] = {0};
  usb_transfer_t* transfer;
  esp_err_t err = usb_host_transfer_alloc(256 + 8, 0, &transfer);
  if (err != ESP_OK) return false;

  // Assemble Control Transfer
  transfer->device_handle = _deviceHandle;
  transfer->bEndpointAddress = 0;
  transfer->callback = [](usb_transfer_t* t) {
    // Set flag in callback
    *(bool*)t->context = true;
  };
  bool done = false;
  transfer->context = &done;
  transfer->timeout_ms = 1000;

  // Setup packet for GET_DEVICE_ID
  usb_setup_packet_t* setup = (usb_setup_packet_t*)transfer->data_buffer;
  setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_IN |
                         USB_BM_REQUEST_TYPE_TYPE_CLASS |
                         USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
  setup->bRequest = USB_PRINTER_GET_DEVICE_ID;
  setup->wValue = 0;                // Config index
  setup->wIndex = _ifaceNum;        // Interface
  setup->wLength = 256;
  transfer->num_bytes = 256 + sizeof(usb_setup_packet_t);

  err = usb_host_transfer_submit_control(_clientHandle, transfer);
  if (err == ESP_OK) {
    // Wait for response
    int timeout = 100;
    while (!done && timeout-- > 0) {
      usb_host_client_handle_events(_clientHandle, 10);
    }

    if (done && transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
      uint8_t* data = transfer->data_buffer + sizeof(usb_setup_packet_t);
      int len = (data[0] << 8) | data[1];
      if (len > 2 && len < 254) {
        _deviceId = String((char*)(data + 2), len - 2);
        Serial.printf("[USB] Device ID: %s\n", _deviceId.c_str());
      }
    }
  }

  usb_host_transfer_free(transfer);
  return _deviceId.length() > 0;
}

int UsbPrinter::sendData(const uint8_t* data, size_t length) {
  if (_state != PRINTER_READY || _deviceHandle == nullptr || _epOut == 0) {
    return -1;
  }

  _state = PRINTER_BUSY;
  size_t totalSent = 0;

  // Send in chunks, each at most _epOutMPS
  while (totalSent < length) {
    size_t chunkSize = min((size_t)_epOutMPS, length - totalSent);

    // Allocate transfer (must be aligned to MPS)
    size_t allocSize = ((chunkSize + _epOutMPS - 1) / _epOutMPS) * _epOutMPS;
    usb_transfer_t* transfer;
    esp_err_t err = usb_host_transfer_alloc(allocSize, 0, &transfer);
    if (err != ESP_OK) {
      _state = PRINTER_READY;
      return totalSent;
    }

    memcpy(transfer->data_buffer, data + totalSent, chunkSize);
    transfer->device_handle = _deviceHandle;
    transfer->bEndpointAddress = _epOut;
    transfer->num_bytes = chunkSize;
    transfer->timeout_ms = 5000;

    volatile bool done = false;
    transfer->callback = [](usb_transfer_t* t) {
      *(volatile bool*)t->context = true;
    };
    transfer->context = (void*)&done;

    err = usb_host_transfer_submit(transfer);
    if (err != ESP_OK) {
      usb_host_transfer_free(transfer);
      _state = PRINTER_READY;
      return totalSent;
    }

    // Wait for transfer to complete
    int timeout = 500;  // 5 seconds
    while (!done && timeout-- > 0) {
      usb_host_client_handle_events(_clientHandle, 10);
    }

    bool success = done && (transfer->status == USB_TRANSFER_STATUS_COMPLETED);
    size_t sent = transfer->actual_num_bytes;
    usb_host_transfer_free(transfer);

    if (!success) {
      Serial.printf("[USB] Send failed, sent %d / %d bytes\n", totalSent, length);
      _state = PRINTER_READY;
      return totalSent;
    }

    totalSent += sent;
  }

  _state = PRINTER_READY;
  return totalSent;
}

bool UsbPrinter::softReset() {
  if (_deviceHandle == nullptr) return false;

  usb_transfer_t* transfer;
  esp_err_t err = usb_host_transfer_alloc(8, 0, &transfer);
  if (err != ESP_OK) return false;

  transfer->device_handle = _deviceHandle;
  transfer->bEndpointAddress = 0;

  usb_setup_packet_t* setup = (usb_setup_packet_t*)transfer->data_buffer;
  setup->bmRequestType = USB_BM_REQUEST_TYPE_DIR_OUT |
                         USB_BM_REQUEST_TYPE_TYPE_CLASS |
                         USB_BM_REQUEST_TYPE_RECIP_INTERFACE;
  setup->bRequest = USB_PRINTER_SOFT_RESET;
  setup->wValue = 0;
  setup->wIndex = _ifaceNum;
  setup->wLength = 0;
  transfer->num_bytes = sizeof(usb_setup_packet_t);
  transfer->timeout_ms = 1000;

  volatile bool done = false;
  transfer->callback = [](usb_transfer_t* t) {
    *(volatile bool*)t->context = true;
  };
  transfer->context = (void*)&done;

  err = usb_host_transfer_submit_control(_clientHandle, transfer);
  if (err == ESP_OK) {
    int timeout = 100;
    while (!done && timeout-- > 0) {
      usb_host_client_handle_events(_clientHandle, 10);
    }
  }

  bool success = done && transfer->status == USB_TRANSFER_STATUS_COMPLETED;
  usb_host_transfer_free(transfer);

  if (success) {
    Serial.println("[USB] Printer reset");
  }
  return success;
}
