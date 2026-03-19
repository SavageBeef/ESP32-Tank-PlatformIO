#include "NetworkHelper.h"
// Secrets
#include "ESP32_secrets.h"

Preferences preferences;
WiFiManager wm;

// Initialize the server on port 80
NetworkHelper::NetworkHelper() : _server(80) {}

// Blynk credentials variables
char blynk_auth[34] = Secret_AUTH;
char blynk_server[40] = Secret_IP;
char blynk_port[6] = Secret_PORT;

// Blynk connection tracking
int blynk_connection_attempts = 0;
const int BLYNK_MAX_ATTEMPTS = 5;
bool has_provisioned_blynk = false;

void NetworkHelper::begin(const char *baseName, const char *password)
{
  // Generate and store the dynamic strings
  _password = String(password);
  _wmAPName = String(baseName) + "-Setup";
  _blynkAPName = String(baseName) + "-Blynk-Setup";
  _otaHost = String(baseName) + "-NodeMCU-32S";

  LittleFS.begin();

  // --- 1. Load Custom Params (Blynk) from Flash ---
  Serial.println("Loading preferences...");
  preferences.begin("tank_config", false);
  String savedAuth = preferences.getString("auth", "");
  String savedServer = preferences.getString("server", "");
  String savedPort = preferences.getString("port", "");

  // Use saved values if available, otherwise use Secret defaults
  if (savedAuth != "")
    savedAuth.toCharArray(blynk_auth, 34);
  if (savedServer != "")
    savedServer.toCharArray(blynk_server, 40);
  if (savedPort != "")
    savedPort.toCharArray(blynk_port, 6);

  Serial.printf("Loaded Blynk Server: %s:%s\n", blynk_server, blynk_port);
  Serial.printf("Loaded Blynk Auth: %.4s...\n", blynk_auth);

  preferences.end();

  // --- 2. WiFi CONNECTION ATTEMPTS ---
  Serial.println("\n--- WiFi Connection Phase ---");
  Serial.println("Attempting to connect to saved Wi-Fi...");

  WiFi.mode(WIFI_STA);
  WiFi.begin();

  int wifi_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && wifi_attempts < 5)
  {
    delay(12000);
    Serial.print(".");
    wifi_attempts++;
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    // WiFi FAILED: Launch combined provisioner for WiFi + Blynk credentials
    Serial.println("\nWiFi connection failed. Launching provisioner for WiFi and Blynk configuration...");
    launchCombinedProvisioner();

    if (!WiFi.isConnected())
    {
      Serial.println("WiFi provisioning failed. Restarting...");
      delay(3000);
      ESP.restart();
    }
  }

  Serial.println("\nWiFi connected!");
  Serial.println("IP: " + WiFi.localIP().toString());

  // --- 3. NOW SETUP OTA AND WEBSERIAL (after WiFi is established) ---
  Serial.println("\n--- Initializing Services ---");
  Serial.println("Setting up Arduino OTA...");
  setupOTA();

  Serial.println("Setting up WebSerial...");
  setupWebSerial();
  Serial.println("WebSerial ready");
}

void NetworkHelper::setupWebSerial()
{
  // WebSerial is accessible at "<IP Address>/webserial" in browser
  WebSerial.begin(&_server);
  WebSerial.println("Network Engine Started");

  /* Attach Message Callback */
  WebSerial.onMessage([&](uint8_t *data, size_t len){
    Serial.printf("Received %u bytes from WebSerial: ", len);
    Serial.write(data, len);
    Serial.println();
    WebSerial.println("Received Data...");
    String d = "";
    for(size_t i=0; i < len; i++){
      d += char(data[i]);
    }
    WebSerial.println(d); 
  });

  // Start server
  _server.begin();
}

void NetworkHelper::setupOTA()
{
  Serial.println("Booting");
  ArduinoOTA.setHostname(_otaHost.c_str());

  ArduinoOTA
    .onStart([](){
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type); 
    })
    .onEnd([]()
      { Serial.println("\nEnd"); })
    .onProgress([](unsigned int progress, unsigned int total)
      { Serial.printf("Progress: %u%%\r", (progress / (total / 100))); })
    .onError([](ota_error_t error){
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
      else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
      else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
      else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
      else if (error == OTA_END_ERROR) Serial.println("End Failed"); 
    });

  ArduinoOTA.begin();

  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void NetworkHelper::handle()
{
  ArduinoOTA.handle(); // Listen for Arduino code upload OTA.
}

// Combined provisioner for WiFi + Blynk on initial WiFi connection failure
void NetworkHelper::launchCombinedProvisioner()
{
  Serial.println("\n=== WiFi + Blynk Configuration Portal ===\n");

  // Combine server and port for display
  char server_port_combined[46];
  snprintf(server_port_combined, sizeof(server_port_combined), "%s:%s", blynk_server, blynk_port);

  // Create custom parameters for Blynk config - separate server:port and auth
  WiFiManagerParameter custom_blynk_server("blynk_server", "Blynk Server (IP:PORT)", server_port_combined, 40);
  WiFiManagerParameter custom_blynk_auth("blynk_auth", "Blynk Auth Token", blynk_auth, 34);

  // Add custom parameters to WiFiManager
  wm.addParameter(&custom_blynk_server);
  wm.addParameter(&custom_blynk_auth);

  // Set callback for when config is saved
  wm.setSaveConfigCallback([]()
    { Serial.println("Config saved - parsing Blynk settings..."); });

  Serial.println("Starting WiFi Configuration Portal...");
  Serial.println("Connect to AP: " + _wmAPName);
  Serial.println("Open browser to: http://192.168.4.1\n");
  Serial.println("Enter:");
  Serial.println("  - WiFi SSID");
  Serial.println("  - WiFi Password");
  Serial.println("  - Blynk Server: IP:PORT format");
  Serial.println("    Example: 192.168.1.200:8080");
  Serial.println("  - Blynk Auth Token");
  Serial.println("    Example: YourAuthTokenHere\n");

  // Set timeout and start portal
  wm.setConfigPortalTimeout(300); // 5 minutes timeout

  // Start the portal with custom AP name and password
  bool res = wm.startConfigPortal(_wmAPName.c_str(), _password.c_str());

  if (res)
  {
    Serial.println("\nPortal connection successful!");

    // Get the Blynk server config (IP:PORT format)
    String blynkServerConfig = custom_blynk_server.getValue();
    String blynkAuthToken = custom_blynk_auth.getValue();

    if (blynkServerConfig.length() > 0)
    {
      Serial.println("Parsing Blynk configuration...");

      // Parse server:port
      int colonPos = blynkServerConfig.indexOf(':');

      if (colonPos > 0)
      {
        String server = blynkServerConfig.substring(0, colonPos);
        String port = blynkServerConfig.substring(colonPos + 1);
        String auth = blynkAuthToken;

        Serial.println("Parsed Blynk Configuration:");
        Serial.print("  Server: ");
        Serial.println(server);
        Serial.print("  Port: ");
        Serial.println(port);
        Serial.print("  Auth: ");
        Serial.println(auth);

        // Update global variables
        server.toCharArray(blynk_server, 40);
        port.toCharArray(blynk_port, 6);
        auth.toCharArray(blynk_auth, 34);

        // Save to preferences
        preferences.begin("tank_config", false);
        preferences.putString("server", server);
        preferences.putString("port", port);
        preferences.putString("auth", auth);
        preferences.end();
      }
      else
      {
        Serial.println("Warning: Blynk config format invalid. Using defaults from secrets.");
      }
    }
    else
    {
      Serial.println("No Blynk config provided. Using defaults from secrets.");
    }
  }
  else
  {
    Serial.println("Portal timeout or failed!");
  }
}

// Blynk-only provisioner for subsequent Blynk connection failures
void NetworkHelper::launchBlynkProvisioner()
{
  Serial.println("\n=== Blynk Server Configuration Portal ===\n");

  if (!LittleFS.begin())
  {
    Serial.println("LittleFS Mount Failed!");
    return;
  }

  // Temporarily stop the main AsyncWebServer to free port 80
  Serial.println("Stopping WebSerial server to free port 80...");
  _server.end();
  delay(500);

  // Restart timer variables
  bool shouldRestart = false;
  unsigned long restartTimer = 0;

  // Create soft AP for Blynk configuration
  WiFi.softAP(_blynkAPName.c_str(), _password.c_str());
  IPAddress softAPIP = WiFi.softAPIP();
  Serial.printf("Soft AP IP: %s\n", softAPIP.toString().c_str());

  // Create a simple AsyncWebServer for Blynk config only (no WiFi fields)
  AsyncWebServer blynkServer(80);

  // Serve the Blynk configuration Bootstrap HTML form with current values pre-filled
  // The lambda [this] captures the class members
  blynkServer.on("/", HTTP_GET, [this](AsyncWebServerRequest *request)
    { request->send(LittleFS, "/blynk.html", String(), false, [this](const String &var) -> String {
      if (var == "CURRENT_SERVER") {
        return String(blynk_server) + ":" + String(blynk_port);
      }
      if (var == "CURRENT_AUTH") {
        return String(blynk_auth);
      }
      if (var == "AP_NAME") {
        return _blynkAPName; // Now accessible because of [this]
      }
      return String(); 
      }); 
    });

  // Serve Bootstrap CSS (Static files from /data)
  blynkServer.serveStatic("/", LittleFS, "/");

  // Handle form submission
  blynkServer.on("/save", HTTP_POST, [&](AsyncWebServerRequest *request){
    if (request->hasParam("server", true) && request->hasParam("auth", true)) {
      String serverParam = request->getParam("server", true)->value();
      String authParam = request->getParam("auth", true)->value();
      
      Serial.println("Blynk config received!");
      Serial.printf("Server: %s\n", serverParam.c_str());
      Serial.printf("Auth: %s\n", authParam.c_str());
      
      // Parse server:port
      int colonPos = serverParam.indexOf(':');
      if (colonPos > 0) {
        String server = serverParam.substring(0, colonPos);
        String port = serverParam.substring(colonPos + 1);
        
        // Update globals
        server.toCharArray(blynk_server, 40);
        port.toCharArray(blynk_port, 6);
        authParam.toCharArray(blynk_auth, 34);
        
        // Save to preferences
        preferences.begin("tank_config", false);
        preferences.putString("server", server);
        preferences.putString("port", port);
        preferences.putString("auth", authParam);
        preferences.end();
        
        //Success page redirect
        request->send(LittleFS, "/success.html", "text/html");
        // Restart logic
        shouldRestart = true;
        restartTimer = millis();
      } else {
        // Error redirect
        Serial.println("Invalid format received. Redirecting back...");
        request->redirect("/?error=1");
      }
    } 
  });

  blynkServer.begin();
  Serial.println("Blynk provisioning server started at http://192.168.4.1");
  Serial.println("Configuration timeout: 5 minutes\n");

  // Wait for configuration (5 minutes timeout)
  unsigned long startTime = millis();
  while (millis() - startTime < 300000)
  { // 5 minutes
    delay(100);
    // Check if we need to restart
    if (shouldRestart && (millis() - restartTimer > 2000))
    {
      ESP.restart();
    }
  }

  blynkServer.end();
  WiFi.softAPdisconnect(true); // Turn off soft AP
  Serial.println("Blynk provisioner timeout - exiting...");

  // Restart the main WebSerial server
  Serial.println("Restarting WebSerial server...");
  _server.begin();
}