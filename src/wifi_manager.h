#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <Preferences.h>
#include "config.h"

enum WifiState {
  WSTATE_NONE,
  WSTATE_AP_SETUP,   // AP mode (for setup)
  WSTATE_STATION     // Connected to home WiFi
};

class WiFiManager {
public:
  WiFiManager();

  // Initialize: try connecting to saved WiFi, fall back to AP mode
  void begin();

  // Main loop processing (auto-reconnect)
  void task();

  // Set new WiFi credentials and save
  bool setCredentials(const String& ssid, const String& password);

  // Get status
  WifiState getMode() const { return _mode; }
  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }
  String getIP() const;
  String getSSID() const { return _ssid; }
  String getHostname() const { return _hostname; }
  int getRSSI() const { return WiFi.RSSI(); }

  // Scan available WiFi networks
  String scanNetworks();

  // Reconnect
  void reconnect();

private:
  WifiState _mode;
  String _ssid;
  String _password;
  String _hostname;
  Preferences _prefs;

  unsigned long _lastReconnectAttempt;
  static const unsigned long RECONNECT_INTERVAL = 10000;  // 10 seconds

  bool _loadCredentials();
  void _saveCredentials();
  bool _connectStation();
  void _startAP();
};

#endif // WIFI_MANAGER_H
