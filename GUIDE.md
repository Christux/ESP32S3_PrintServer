# ESP32-S3 WiFi Print Server — Complete Guide

> Turn a USB-only printer into a WiFi network printer using an ESP32-S3 dev board.

---

## Hardware Requirements

| Item | Specification | Notes |
|------|---------------|-------|
| Dev board | ESP32-S3-N16R8 (dual Type-C) | 16MB Flash, 8MB PSRAM |
| Printer | Brother HL-2030 | USB connection |
| USB OTG adapter | Type-C male → USB-A female | **Key component** |
| USB cable | USB-A male → USB-B male | Printer's original cable |
| Power supply | 5V USB charger + Type-C cable | Powers the ESP32 |

### Wiring

```
[Mac] ~~~WiFi~~~ [ESP32-S3] ---USB OTG--- [Brother HL-2030]
                     |
              (other Type-C for power/programming)
```

**ESP32-S3 dual Type-C ports:**
- **USB port (labeled USB):** Connect OTG adapter → connect printer (USB Host mode)
- **UART port (labeled COM/UART):** Connect to computer for flashing, then use for power

> ⚠️ **Important:** The USB OTG adapter must be a "Type-C male → USB-A female" OTG cable, not a regular charging cable. Search for "USB-C OTG adapter" — costs about $2-3.

---

## Software Setup

### Step 1: Install Arduino IDE

1. Download and install [Arduino IDE 2.x](https://www.arduino.cc/en/software)
2. Open Arduino IDE

### Step 2: Install ESP32 Board Support

1. **File → Preferences**
2. In **Additional Board Manager URLs** add:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. **Tools → Board → Board Manager**
4. Search `esp32`, install **esp32 by Espressif Systems** (latest version)

### Step 3: Configure Board

Set in the **Tools** menu:

| Setting | Value |
|---------|-------|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Disabled |
| USB Mode | USB-OTG (TinyUSB) |
| PSRAM | OPI PSRAM |
| Flash Size | 16MB |
| Partition Scheme | OTA-capable partition scheme with two app slots |
| Upload Mode | UART0 / Hardware CDC |
| Port | Select your COM port |

> ⚠️ **USB Mode must be USB-OTG (TinyUSB)** so the USB port works as Host.

### Step 4: Open Project

1. Open `ESP32S3_PrintServer.ino` with Arduino IDE
2. The `.h` and `.cpp` files in the same folder will load automatically
3. Confirm all tabs appear (config.h, usb_printer.h/cpp, wifi_manager.h/cpp, print_server.h/cpp, web_ui.h/cpp)

### Step 5: Flash

1. Connect ESP32's **UART port** to computer with Type-C cable
2. Press **Upload** (→ button)
3. If flashing fails, hold the board's **BOOT** button, press **RST**, then release RST, wait a few seconds, release BOOT

---

## First-Time Setup

### Step 1: Connect Printer

1. After flashing, connect the printer to ESP32's **USB port** via OTG adapter
2. Connect the other Type-C port to 5V power (or keep connected to computer)
3. Open Arduino IDE Serial Monitor (115200 baud)
4. You should see something like:
   ```
   [USB] New device detected at address: 1
   [USB] VID: 0x04F9, PID: 0x002D
   [USB] Manufacturer: Brother
   [USB] Product: HL-2030 series
   [USB] ✓ Printer ready
   ```

### Step 2: Configure WiFi

1. Search for WiFi on your phone or computer, find **PrintServer-Setup**
2. Password: **12345678**
3. After connecting, open **http://192.168.4.1** in your browser
4. In the management UI:
   - Click **Scan** to scan available WiFi networks
   - Select your home WiFi
   - Enter password
   - Click **Connect**
5. After successful connection, the page will show the new IP address

### Step 3: Mac Setup

1. First confirm Mac has Brother HL-2030 driver installed
   - Download: [Brother official driver page](https://support.brother.com/g/b/downloadtop.aspx?c=us_ot&lang=en&prod=hl2030_us_eu_as)
   - Or macOS may already include it

2. Open **System Settings → Printers & Scanners**

3. Click **Add Printer** (+ button)

4. Select the **IP** tab

5. Fill in:
   | Field | Value |
   |-------|-------|
   | Protocol | HP Jetdirect - Socket |
   | Address | `hl2030.local` or ESP32's IP |
   | Queue | Leave empty |
   | Name | Brother HL-2030 WiFi |
   | Use | Select Brother HL-2030 driver |

6. Click **Add**

7. Done! Now you can print from any app by selecting this printer.

---

## Daily Use

- **Printing:** Like any network printer — select the printer from Mac to print directly
- **Management UI:** Open `http://hl2030.local` in browser to check status
- **Printer unresponsive:** Click Restart in the management UI, or unplug/replug ESP32 power
- **OTA Update:** When selecting Port in Arduino IDE, you'll see `printserver` (network) — firmware can be updated wirelessly

---

## Troubleshooting

### Printer not detected

- Confirm you're using an OTG adapter cable (not a regular charging cable)
- Confirm it's plugged into the USB port (not the UART port)
- Try unplugging and replugging the USB cable
- Check Serial Monitor for `[USB]` messages

### WiFi won't connect

- Confirm SSID and password are correct
- ESP32 only supports **2.4GHz WiFi**, not 5GHz
- If completely stuck, holding BOOT for 10 seconds can reset WiFi settings (requires implementing this in firmware)

### Mac can't find printer

- Confirm Mac and ESP32 are on the same WiFi network
- Try using IP address instead of `.local`
- Confirm Brother driver is installed
- Test connection from Terminal: `nc -z <ESP32_IP> 9100` (should connect successfully)

### Prints garbled text or blank pages

- Confirm the driver selected on Mac is Brother HL-2030 (not Generic PostScript)
- Protocol must be **HP Jetdirect - Socket**, not IPP or LPD

---

## Advanced: Modifying Settings

Edit `config.h` to adjust:

- `AP_SSID` / `AP_PASSWORD`: AP mode name and password
- `MDNS_HOSTNAME`: mDNS name (default `hl2030`)
- `RAW_SOCKET_PORT`: Print port (default 9100, usually no need to change)
- `PRINT_BUFFER_SIZE`: Print buffer size (default 512KB)
- `LED_PIN`: Status LED pin

---

## System Architecture

```
┌─────────────┐         WiFi          ┌──────────────────┐
│    Mac      │ ◄───────────────────► │    ESP32-S3      │
│             │   Raw Socket :9100    │                  │
│ Brother     │                       │ ┌──────────────┐ │     USB     ┌────────────┐
│ Driver      │ ── print data ────►   │ │ Print Server │ │ ──────────► │ Brother    │
│             │                       │ └──────────────┘ │ USB Host    │ HL-2030    │
│ Safari      │ ◄──── HTTP ────────►  │ ┌──────────────┐ │             └────────────┘
│             │   Management :80      │ │   Web UI     │ │
└─────────────┘                       │ └──────────────┘ │
                                      │ ┌──────────────┐ │
                                      │ │ WiFi Manager │ │
                                      │ └──────────────┘ │
                                      │ ┌──────────────┐ │
                                      │ │  mDNS + OTA  │ │
                                      │ └──────────────┘ │
                                      └──────────────────┘
```

---

## Parts Purchase Suggestions

1. **USB-C OTG Adapter** (Type-C male → USB-A female)
   - Search "USB-C OTG adapter" online, about $2-3
   - Confirm it supports OTG / Host mode

2. **5V USB Charger** (for powering ESP32)
   - Any phone charger works, 1A or higher

---

*Created: 2026-05-20*
