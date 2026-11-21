#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"

/** Operating mode of the WiFi manager. */
enum WifiState {
  WSTATE_NONE,
  WSTATE_AP_SETUP,   // AP mode (for setup)
  WSTATE_STATION     // Connected to home WiFi
};

/** Manages station and setup-access-point WiFi modes. */
class WiFiManager {
public:
  /** Constructs the WiFi manager. */
  WiFiManager();

  /**
   * Initializes WiFi using saved credentials or setup AP mode.
   *
   * Saved station credentials are attempted first. If no credentials are
   * available or the connection fails, the setup access point is started.
   */
  void begin();

  /** Processes automatic reconnection checks. Call continuously from loop(). */
  void task();

  /**
   * Saves credentials and attempts a station connection.
   *
   * @param ssid WiFi network name.
   * @param password WiFi network password.
   * @return true if the station connection succeeds, false otherwise.
   */
  bool setCredentials(const String& ssid, const String& password);

  /** Returns the current operating mode. */
  WifiState getMode() const { return _mode; }

  /** Reports whether the ESP32 is connected to a WiFi station. */
  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

  /** Returns the active station or setup-AP IP address. */
  String getIP() const;

  /** Returns the configured station SSID. */
  String getSSID() const { return _ssid; }

  /** Returns the configured mDNS hostname. */
  String getHostname() const { return _hostname; }

  /** Returns the received signal strength of the station connection. */
  int getRSSI() const { return WiFi.RSSI(); }

  /**
   * Scans for nearby networks and returns them as a JSON array.
   *
   * @return JSON describing each network's SSID, RSSI, and security state.
   */
  String scanNetworks();

  /** Disconnects and retries the configured station connection. */
  void reconnect();

private:
  WifiState _mode;
  String _ssid;
  String _password;
  String _hostname;
  Preferences _prefs;

  unsigned long _lastReconnectAttempt;
  static const unsigned long RECONNECT_INTERVAL = 10000;  // 10 seconds

  /** Loads station credentials and hostname from nonvolatile storage. */
  bool _loadCredentials();

  /** Saves station credentials and hostname to nonvolatile storage. */
  void _saveCredentials();

  /** Attempts to establish the configured station connection. */
  bool _connectStation();

  /** Starts the fallback setup access point. */
  void _startAP();
};

#endif // WIFI_MANAGER_H
