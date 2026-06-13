#ifndef NETWORK_HELPER_H
#define NETWORK_HELPER_H

#include <Arduino.h>
#include <WiFi.h>
// Include WebServer FIRST to prevent conflicts
#include <WebServer.h>
// Arduino OTA
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
// WiFiManager - include after WebServer
#include <WiFiManager.h> // WiFi Configuration Captive Portal
// webSerialMonitor - include after WiFiManager
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <WebSerial.h>

#include <Preferences.h> // Native ESP32 library for saving data to flash memory.
#include <LittleFS.h>

// EXTERN DECLARATIONS
// This exposes the variables from NetworkHelper.cpp to any file that includes this header
extern char blynk_auth[34];
extern char blynk_server[40];
extern char blynk_port[6];
extern int blynk_connection_attempts;
extern const int BLYNK_MAX_ATTEMPTS;
extern bool has_provisioned_blynk;

class NetworkHelper {
public:
    NetworkHelper();
    void begin(const char* baseName, const char* password);
    void handle(); // To be called in loop()
    // Add a way to access the server if other libs need it
    AsyncWebServer* getServer() { return &_server; }
    void launchBlynkProvisioner();
    bool connectToStrongestAP(const char* targetSSID, const char* targetPass);

private:
    AsyncWebServer _server;
    // Storage for our dynamic identifiers
    String _password;
    String _otaHost;
    String _blynkAPName;
    String _wmAPName;
    void setupOTA();
    void setupWebSerial();
    void launchCombinedProvisioner();

};

#endif