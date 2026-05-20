#include "usb_printer.h"

static const char* TAG = "USB_PRINTER";

UsbPrinter::UsbPrinter()
  : _state(PRINTER_DISCONNECTED),
    _clientHandle(nullptr),
    _deviceHandle(nullptr),
    _epOut(0), _epIn(0), _epOutMPS(64),
    _ifaceNum(0) {}

bool UsbPrinter::begin() {
  // 安裝 USB Host Library
  usb_host_config_t hostConfig = {
    .skip_phy_setup = false,
    .intr_flags = ESP_INTR_FLAG_LEVEL1,
  };

  esp_err_t err = usb_host_install(&hostConfig);
  if (err != ESP_OK) {
    Serial.printf("[USB] Host install failed: %s\n", esp_err_to_name(err));
    return false;
  }

  // 建立 USB Host Client
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

  Serial.println("[USB] Host 初始化完成，等待印表機連接...");
  return true;
}

void UsbPrinter::task() {
  if (_clientHandle == nullptr) return;

  // 處理 USB Host Library 事件
  usb_host_lib_handle_events(0, NULL);

  // 處理 Client 事件
  usb_host_client_handle_events(_clientHandle, 0);
}

void UsbPrinter::_clientEventCallback(const usb_host_client_event_msg_t* eventMsg, void* arg) {
  UsbPrinter* self = (UsbPrinter*)arg;
  self->_handleClientEvent(eventMsg);
}

void UsbPrinter::_handleClientEvent(const usb_host_client_event_msg_t* eventMsg) {
  switch (eventMsg->event) {
    case USB_HOST_CLIENT_EVENT_NEW_DEV:
      Serial.printf("[USB] 偵測到新裝置，位址: %d\n", eventMsg->new_dev.address);
      if (_state == PRINTER_DISCONNECTED) {
        _openDevice(eventMsg->new_dev.address);
      }
      break;

    case USB_HOST_CLIENT_EVENT_DEV_GONE:
      Serial.println("[USB] 裝置已斷開");
      _closeDevice();
      break;

    default:
      break;
  }
}

bool UsbPrinter::_openDevice(uint8_t devAddr) {
  esp_err_t err = usb_host_device_open(_clientHandle, devAddr, &_deviceHandle);
  if (err != ESP_OK) {
    Serial.printf("[USB] 無法開啟裝置: %s\n", esp_err_to_name(err));
    return false;
  }

  _state = PRINTER_CONNECTED;

  // 取得裝置描述符
  const usb_device_desc_t* devDesc;
  err = usb_host_get_device_descriptor(_deviceHandle, &devDesc);
  if (err == ESP_OK) {
    Serial.printf("[USB] VID: 0x%04X, PID: 0x%04X\n", devDesc->idVendor, devDesc->idProduct);
  }

  // 暫時使用 VID/PID 識別（字串描述符需要額外的 control transfer）
  if (devDesc->idVendor == 0x04F9) {
    _manufacturer = "Brother";
    if (devDesc->idProduct == 0x002D || devDesc->idProduct == 0x0074) {
      _product = "HL-1110 series";
    } else {
      _product = "Brother Printer";
    }
  } else {
    _manufacturer = String("VID:0x") + String(devDesc->idVendor, HEX);
    _product = String("PID:0x") + String(devDesc->idProduct, HEX);
  }
  Serial.printf("[USB] 製造商: %s\n", _manufacturer.c_str());
  Serial.printf("[USB] 產品: %s\n", _product.c_str());

  // 尋找 Printer 介面
  if (!_findPrinterInterface()) {
    Serial.println("[USB] 找不到 Printer 介面，這不是印表機");
    _closeDevice();
    return false;
  }

  // 宣告介面
  if (!_claimInterface()) {
    Serial.println("[USB] 無法宣告印表機介面");
    _closeDevice();
    return false;
  }

  // 取得 Device ID
  _getDeviceId();

  _state = PRINTER_READY;
  Serial.println("[USB] ✓ 印表機已就緒");
  return true;
}

bool UsbPrinter::_findPrinterInterface() {
  const usb_config_desc_t* configDesc;
  esp_err_t err = usb_host_get_active_config_descriptor(_deviceHandle, &configDesc);
  if (err != ESP_OK) {
    Serial.printf("[USB] 取得設定描述符失敗: %s\n", esp_err_to_name(err));
    return false;
  }

  // 遍歷所有介面描述符，找到 Printer Class
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
        Serial.printf("[USB] 找到 Printer 介面 #%d, Protocol: %d\n",
                      _ifaceNum, ifaceDesc->bInterfaceProtocol);

        // 尋找此介面的端點
        int epOffset = offset + desc->bLength;
        for (int ep = 0; ep < ifaceDesc->bNumEndpoints && epOffset < totalLen; ep++) {
          const usb_standard_desc_t* epDescRaw = (const usb_standard_desc_t*)(p + epOffset);
          if (epDescRaw->bDescriptorType == USB_B_DESCRIPTOR_TYPE_ENDPOINT) {
            const usb_ep_desc_t* epDesc = (const usb_ep_desc_t*)epDescRaw;

            if ((epDesc->bmAttributes & 0x03) == USB_BM_ATTRIBUTES_XFER_BULK) {
              if (epDesc->bEndpointAddress & 0x80) {
                // IN 端點
                _epIn = epDesc->bEndpointAddress;
                Serial.printf("[USB] Bulk IN 端點: 0x%02X\n", _epIn);
              } else {
                // OUT 端點
                _epOut = epDesc->bEndpointAddress;
                _epOutMPS = epDesc->wMaxPacketSize;
                Serial.printf("[USB] Bulk OUT 端點: 0x%02X, MPS: %d\n", _epOut, _epOutMPS);
              }
            }
          }
          epOffset += epDescRaw->bLength;
        }

        return (_epOut != 0);  // 至少需要 OUT 端點
      }
    }
    offset += desc->bLength;
  }

  return false;
}

bool UsbPrinter::_claimInterface() {
  esp_err_t err = usb_host_interface_claim(_clientHandle, _deviceHandle, _ifaceNum, 0);
  if (err != ESP_OK) {
    Serial.printf("[USB] 宣告介面失敗: %s\n", esp_err_to_name(err));
    return false;
  }
  Serial.printf("[USB] 已宣告介面 #%d\n", _ifaceNum);
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
  Serial.println("[USB] 裝置已關閉");
}

bool UsbPrinter::_getDeviceId() {
  if (_deviceHandle == nullptr) return false;

  // USB Printer Class: GET_DEVICE_ID 請求
  uint8_t buf[256] = {0};
  usb_transfer_t* transfer;
  esp_err_t err = usb_host_transfer_alloc(256 + 8, 0, &transfer);
  if (err != ESP_OK) return false;

  // 組裝 Control Transfer
  transfer->device_handle = _deviceHandle;
  transfer->bEndpointAddress = 0;
  transfer->callback = [](usb_transfer_t* t) {
    // 回呼中設定旗標
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
    // 等待回覆
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

  // 分段傳送，每段最多 _epOutMPS
  while (totalSent < length) {
    size_t chunkSize = min((size_t)_epOutMPS, length - totalSent);

    // 配置 transfer（需要對齊到 MPS）
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

    // 等待傳送完成
    int timeout = 500;  // 5 秒
    while (!done && timeout-- > 0) {
      usb_host_client_handle_events(_clientHandle, 10);
    }

    bool success = done && (transfer->status == USB_TRANSFER_STATUS_COMPLETED);
    size_t sent = transfer->actual_num_bytes;
    usb_host_transfer_free(transfer);

    if (!success) {
      Serial.printf("[USB] 傳送失敗，已送 %d / %d bytes\n", totalSent, length);
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
    Serial.println("[USB] 印表機已重置");
  }
  return success;
}
